#define _GNU_SOURCE

#include "plugin_api_v1.h"
#include "scratch_engine.h"
#include "vendor/xwax/timecoder.h"

#include <math.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct wav_header {
    char riff[4];
    uint32_t riff_size;
    char wave[4];
} wav_header_t;

typedef struct scratch_instance {
    scratch_engine_t engine;
    struct timecoder decoder;
    pthread_t loader_thread;
    atomic_int loader_state; /* 0 no file/loading, 1 ready, -1 error */
    atomic_int decoder_ready;
    atomic_int loader_stop;
    atomic_uint path_sequence;
    char requested_path[PATH_MAX];
    _Atomic(int16_t *) published_sample;
    atomic_size_t published_frames;
    _Atomic(unsigned char *) published_waveform;
    atomic_size_t published_waveform_bins;
    _Atomic(unsigned char *) published_overview;
    atomic_uint published_generation;
    atomic_uint adopted_generation;
    unsigned int local_generation;
    const unsigned char *waveform;
    size_t waveform_bins;
    const unsigned char *overview;
    int absolute_mode;
    int fader_cc;
    int fader_channel; /* 0-15, or 16 for omni */
    int hamster;
    int midi_learn;
    int last_cc;
    int last_value;
    int last_note;
    int last_note_source;
    float cut_in;
    float curve;
    float low_cut_hz;
    float retrigger_ms;
    int wave_zoom;
    int jog_active;
    int jog_gate_open;
    int knob_touched;
    int control_mode;
    int virtual_play;
    float jog_sensitivity;
    int jog_touch;
    float knob_sensitivity;
    float virtual_speed;
    float touch_inertia_ms;
    float knob_smoothing_ms;
    int loop;
    float platter_value;
    int platter_initialized;
    unsigned int virtual_samples_remaining;
    unsigned int keepalive_phase;
    float fader_raw;
    uint32_t held_notes[4];
    int pad_gate_active;
    float input_peak_l;
    float input_peak_r;
    double decoded_pitch;
    double decoded_position_seconds;
    double decoded_age_ms;
    float decoded_quality;
    unsigned int decoded_word;
    int decoded_locked;
    unsigned char scope_xy[32];
} scratch_instance_t;

static const host_api_v1_t *g_host;

static uint16_t read_u16_le(const unsigned char *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t read_u32_le(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int json_get_number(const char *json, const char *key, double *out)
{
    char search[64];
    const char *value;
    char *end = NULL;
    snprintf(search, sizeof(search), "\"%s\":", key);
    value = strstr(json, search);
    if (!value) return 0;
    value += strlen(search);
    while (*value == ' ' || *value == '\t') value++;
    *out = strtod(value, &end);
    return end != value;
}

static int json_get_string(const char *json, const char *key,
                           char *out, size_t out_len)
{
    char search[64];
    const char *p;
    size_t n = 0;
    snprintf(search, sizeof(search), "\"%s\":", key);
    p = strstr(json, search);
    if (!p || out_len == 0) return 0;
    p += strlen(search);
    while (*p == ' ' || *p == '\t') p++;
    if (*p++ != '"') return 0;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\' && *p) {
            c = *p++;
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
        }
        if (n + 1 >= out_len) return 0;
        out[n++] = c;
    }
    if (*p != '"') return 0;
    out[n] = '\0';
    return 1;
}

static int json_escape(const char *src, char *out, size_t out_len)
{
    size_t n = 0;
    while (*src) {
        const char *escaped = NULL;
        char c = *src++;
        if (c == '"') escaped = "\\\"";
        else if (c == '\\') escaped = "\\\\";
        else if (c == '\n') escaped = "\\n";
        else if (c == '\r') escaped = "\\r";
        else if (c == '\t') escaped = "\\t";
        if (escaped) {
            if (n + 2 >= out_len) return 0;
            out[n++] = escaped[0];
            out[n++] = escaped[1];
        } else {
            if ((unsigned char)c < 0x20 || n + 1 >= out_len) return 0;
            out[n++] = c;
        }
    }
    out[n] = '\0';
    return 1;
}

static int load_pcm16_stereo_wav(const char *path, int16_t **pcm, size_t *frames)
{
    FILE *file;
    unsigned char header[12];
    unsigned char chunk[8];
    int format_ok = 0;
    uint32_t data_size = 0;
    long data_offset = 0;

    file = fopen(path, "rb");
    if (!file)
        return -1;
    if (fread(header, 1, sizeof(header), file) != sizeof(header) ||
        memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
        goto fail;

    while (fread(chunk, 1, sizeof(chunk), file) == sizeof(chunk)) {
        uint32_t size = read_u32_le(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0) {
            unsigned char fmt[40];
            if (size < 16 || size > sizeof(fmt) || fread(fmt, 1, size, file) != size)
                goto fail;
            format_ok = read_u16_le(fmt) == 1 && read_u16_le(fmt + 2) == 2 &&
                        read_u32_le(fmt + 4) == 44100 && read_u16_le(fmt + 14) == 16;
        } else if (memcmp(chunk, "data", 4) == 0) {
            data_offset = ftell(file);
            data_size = size;
            if (fseek(file, (long)size, SEEK_CUR) != 0)
                goto fail;
        } else if (fseek(file, (long)size, SEEK_CUR) != 0) {
            goto fail;
        }
        if (size & 1)
            fseek(file, 1, SEEK_CUR);
        if (format_ok && data_offset)
            break;
    }

    if (!format_ok || !data_offset || data_size < 4)
        goto fail;

    *pcm = malloc(data_size);
    if (!*pcm)
        goto fail;
    if (fseek(file, data_offset, SEEK_SET) != 0 ||
        fread(*pcm, 1, data_size, file) != data_size) {
        free(*pcm);
        *pcm = NULL;
        goto fail;
    }
    fclose(file);
    *frames = data_size / 4;
    return 0;

fail:
    fclose(file);
    return -1;
}

#define WAVEFORM_BIN_FRAMES 256

static unsigned char *build_waveform(const int16_t *pcm, size_t frames,
                                     size_t *bins_out)
{
    size_t bins = (frames + WAVEFORM_BIN_FRAMES - 1) / WAVEFORM_BIN_FRAMES;
    unsigned char *waveform = calloc(bins ? bins : 1, 1);
    size_t bin;
    if (!waveform)
        return NULL;
    for (bin = 0; bin < bins; ++bin) {
        size_t first = bin * WAVEFORM_BIN_FRAMES;
        size_t last = first + WAVEFORM_BIN_FRAMES;
        int peak = 0;
        size_t frame;
        if (last > frames)
            last = frames;
        for (frame = first; frame < last; ++frame) {
            int left = pcm[frame * 2];
            int right = pcm[frame * 2 + 1];
            if (left < 0) left = -left;
            if (right < 0) right = -right;
            if (left > peak) peak = left;
            if (right > peak) peak = right;
        }
        waveform[bin] = (unsigned char)((peak * 255 + 16383) / 32767);
    }
    *bins_out = bins;
    return waveform;
}

static unsigned char *build_overview(const unsigned char *waveform, size_t bins)
{
    unsigned char *overview = calloc(128, 1);
    int x;
    if (!overview)
        return NULL;
    for (x = 0; x < 128; ++x) {
        size_t first = (size_t)x * bins / 128;
        size_t last = (size_t)(x + 1) * bins / 128;
        unsigned char peak = 0;
        size_t bin;
        if (last <= first && first < bins)
            last = first + 1;
        for (bin = first; bin < last && bin < bins; ++bin) {
            if (waveform[bin] > peak)
                peak = waveform[bin];
        }
        overview[x] = peak;
    }
    return overview;
}

static void demote_loader_thread(void)
{
#ifdef __linux__
    struct sched_param sp = { .sched_priority = 0 };
    cpu_set_t set;
    sched_setscheduler(0, SCHED_OTHER, &sp);
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    CPU_SET(1, &set);
    CPU_SET(2, &set);
    sched_setaffinity(0, sizeof(set), &set);
#endif
}

static void *loader_main(void *opaque)
{
    scratch_instance_t *instance = opaque;
    struct timecode_def *definition;
    unsigned int handled_sequence = 0;
    int16_t *current_sample = NULL;
    unsigned char *current_waveform = NULL;
    unsigned char *current_overview = NULL;

    demote_loader_thread();
    definition = timecoder_find_definition("serato_2a");
    if (definition) {
        timecoder_init(&instance->decoder, definition, 1.0, 44100, false);
        atomic_store_explicit(&instance->decoder_ready, 1, memory_order_release);
    }

    while (!atomic_load_explicit(&instance->loader_stop, memory_order_acquire)) {
        unsigned int before = atomic_load_explicit(&instance->path_sequence,
                                                   memory_order_acquire);
        char path[PATH_MAX];
        unsigned int after;

        if ((before & 1u) || before == handled_sequence) {
            usleep(20000);
            continue;
        }
        memcpy(path, instance->requested_path, sizeof(path));
        path[sizeof(path) - 1] = '\0';
        after = atomic_load_explicit(&instance->path_sequence,
                                     memory_order_acquire);
        if (before != after || (after & 1u))
            continue;
        handled_sequence = after;

        if (path[0] == '\0') {
            atomic_store_explicit(&instance->loader_state, 0,
                                  memory_order_release);
            continue;
        }

        int16_t *next_sample = NULL;
        size_t next_frames = 0;
        unsigned char *next_waveform = NULL;
        unsigned char *next_overview = NULL;
        size_t next_waveform_bins = 0;
        atomic_store_explicit(&instance->loader_state, 0, memory_order_release);
        if (load_pcm16_stereo_wav(path, &next_sample, &next_frames) != 0) {
            atomic_store_explicit(&instance->loader_state, -1,
                                  memory_order_release);
            continue;
        }
        next_waveform = build_waveform(next_sample, next_frames,
                                       &next_waveform_bins);
        if (next_waveform)
            next_overview = build_overview(next_waveform, next_waveform_bins);

        atomic_store_explicit(&instance->published_sample, next_sample,
                              memory_order_relaxed);
        atomic_store_explicit(&instance->published_frames, next_frames,
                              memory_order_relaxed);
        atomic_store_explicit(&instance->published_waveform, next_waveform,
                              memory_order_relaxed);
        atomic_store_explicit(&instance->published_waveform_bins,
                              next_waveform_bins, memory_order_relaxed);
        atomic_store_explicit(&instance->published_overview, next_overview,
                              memory_order_relaxed);
        unsigned int generation = atomic_fetch_add_explicit(
            &instance->published_generation, 1, memory_order_release) + 1;
        atomic_store_explicit(&instance->loader_state, 1, memory_order_release);

        while (!atomic_load_explicit(&instance->loader_stop, memory_order_acquire) &&
               atomic_load_explicit(&instance->adopted_generation,
                                    memory_order_acquire) < generation)
            usleep(1000);
        free(current_sample);
        free(current_waveform);
        free(current_overview);
        current_sample = next_sample;
        current_waveform = next_waveform;
        current_overview = next_overview;
    }
    free(current_sample);
    free(current_waveform);
    free(current_overview);
    return NULL;
}

static float fader_gain(const scratch_instance_t *instance, float raw)
{
    float value = instance->hamster ? 1.0f - raw : raw;
    if (value <= instance->cut_in)
        return 0.0f;
    value = (value - instance->cut_in) / (1.0f - instance->cut_in);
    return powf(value, instance->curve);
}

static void apply_gate(scratch_instance_t *instance)
{
    int any_note = instance->held_notes[0] || instance->held_notes[1] ||
                   instance->held_notes[2] || instance->held_notes[3];
    int jog_gate_active = instance->jog_active;
    float gain;
    if (jog_gate_active || instance->pad_gate_active) {
        /* Hardware pads continue to reach the DSP while a fullscreen canvas
         * is open, but Schwung does not duplicate them to the canvas onMidi
         * hook unless passive pad observation is explicitly enabled. Always
         * honor the DSP's real held-note state; jog_gate_open remains useful
         * for hosts that do duplicate canvas pad messages. */
        int gate_open = any_note || (jog_gate_active && instance->jog_gate_open);
        if (instance->hamster)
            gate_open = !gate_open;
        gain = gate_open ? 1.0f : 0.0f;
    } else {
        gain = fader_gain(instance, instance->fader_raw);
    }
    scratch_engine_set_fader(&instance->engine, gain);
}

static void *scratch_create(const char *module_dir, const char *json_defaults)
{
    scratch_instance_t *instance = calloc(1, sizeof(*instance));
    (void)json_defaults;
    if (!instance)
        return NULL;

    scratch_engine_init(&instance->engine, 44100, 44100);
    instance->fader_cc = 1;
    instance->fader_channel = 16;
    instance->cut_in = 0.03f;
    instance->curve = 1.0f;
    instance->low_cut_hz = 100.0f;
    instance->retrigger_ms = 8.7f;
    instance->wave_zoom = 1;
    instance->jog_sensitivity = 0.55f;
    instance->knob_sensitivity = 0.18f;
    instance->virtual_play = 1;
    instance->virtual_speed = 1.0f;
    instance->touch_inertia_ms = 45.0f;
    instance->knob_smoothing_ms = 25.0f;
    instance->loop = 1;
    instance->platter_value = 0.5f;
    instance->fader_raw = 1.0f;
    instance->pad_gate_active = 1;
    if (module_dir && module_dir[0])
        snprintf(instance->requested_path, sizeof(instance->requested_path),
                 "%s/samples/ahh-fresh.wav", module_dir);
    scratch_engine_set_low_cut(&instance->engine, instance->low_cut_hz);
    scratch_engine_set_loop(&instance->engine, instance->loop);
    apply_gate(instance);

    if (pthread_create(&instance->loader_thread, NULL, loader_main, instance) != 0) {
        free(instance);
        return NULL;
    }
    return instance;
}

static void scratch_destroy(void *opaque)
{
    scratch_instance_t *instance = opaque;
    if (!instance)
        return;
    atomic_store_explicit(&instance->loader_stop, 1, memory_order_release);
    pthread_join(instance->loader_thread, NULL);
    if (atomic_load(&instance->decoder_ready))
        timecoder_clear(&instance->decoder);
    free(instance);
}

static void scratch_on_midi(void *opaque, const uint8_t *msg, int len, int source)
{
    scratch_instance_t *instance = opaque;
    int status;
    int channel;

    if (!instance || !msg || len < 3)
        return;
    status = msg[0] & 0xf0;
    channel = msg[0] & 0x0f;

    /* Knob Scratch is the first physical encoder on Main 2, whose capacitive
     * touch is note 0. Touch holds the virtual record; release restarts its
     * motor. Do this before the pad path so knob touches never open the gate. */
    if (instance->control_mode == 1 && instance->jog_touch && msg[1] == 0 &&
        (status == 0x80 || status == 0x90)) {
        instance->knob_touched = status == 0x90 && msg[2] != 0;
        if (!instance->knob_touched)
            instance->virtual_samples_remaining = 0;
        scratch_engine_set_rate_smooth(&instance->engine,
            instance->knob_touched ? 0.0 :
            (instance->virtual_play ? instance->virtual_speed : 0.0),
            (unsigned int)lrintf(instance->touch_inertia_ms * 44.1f));
        return;
    }

    if (instance->control_mode == 2 && instance->jog_touch && msg[1] == 9 &&
        (status == 0x80 || status == 0x90)) {
        instance->knob_touched = status == 0x90 && msg[2] != 0;
        if (!instance->knob_touched)
            instance->virtual_samples_remaining = 0;
        scratch_engine_set_rate_smooth(&instance->engine,
            instance->knob_touched ? 0.0 :
            (instance->virtual_play ? instance->virtual_speed : 0.0),
            (unsigned int)lrintf(instance->touch_inertia_ms * 44.1f));
        return;
    }

    /* The canvas sees raw physical pads (68-99), but the sound-generator DSP
     * receives the selected layout's translated musical notes. Notes 0-9 are
     * Move's capacitive knob touches; everything above that is playable. */
    if (msg[1] >= 10 &&
        (status == 0x80 || status == 0x90)) {
        unsigned int word = msg[1] >> 5;
        uint32_t bit = 1u << (msg[1] & 31);
        instance->last_note = msg[1];
        instance->last_note_source = source;
        if (status == 0x90 && msg[2] != 0) {
            instance->held_notes[word] |= bit;
            scratch_engine_retrigger(&instance->engine,
                (unsigned int)lrintf(instance->retrigger_ms * 44.1f));
        } else {
            instance->held_notes[word] &= ~bit;
        }
        instance->pad_gate_active = 1;
        apply_gate(instance);
        return;
    }

    if (source != MOVE_MIDI_SOURCE_EXTERNAL)
        return;
    if (status != 0xb0)
        return;
    instance->last_cc = msg[1];
    instance->last_value = msg[2];
    if (instance->midi_learn) {
        instance->fader_cc = msg[1];
        instance->fader_channel = channel;
        instance->midi_learn = 0;
    }
    if (msg[1] != instance->fader_cc ||
        (instance->fader_channel < 16 && channel != instance->fader_channel))
        return;
    instance->fader_raw = msg[2] / 127.0f;
    instance->pad_gate_active = 0;
    apply_gate(instance);
}

static void scratch_set_param(void *opaque, const char *key, const char *value)
{
    scratch_instance_t *instance = opaque;
    if (!instance || !key || !value)
        return;
    if (strcmp(key, "state") == 0) {
        static const char *const numeric_keys[] = {
            "control_mode", "mode", "fader_cc", "fader_channel", "hamster", "cut_in",
            "curve", "low_cut", "retrigger_ms", "wave_zoom",
            "jog_sensitivity", "jog_touch", "knob_sensitivity", "knob_smoothing_ms", "virtual_play",
            "virtual_speed", "touch_inertia_ms", "loop"
        };
        char path[PATH_MAX];
        size_t i;
        if (json_get_string(value, "sample_file", path, sizeof(path)))
            scratch_set_param(instance, "sample_file", path);
        for (i = 0; i < sizeof(numeric_keys) / sizeof(numeric_keys[0]); ++i) {
            double number;
            char scalar[32];
            if (!json_get_number(value, numeric_keys[i], &number)) continue;
            snprintf(scalar, sizeof(scalar), "%.9g", number);
            scratch_set_param(instance, numeric_keys[i], scalar);
        }
        return;
    }
    if (strcmp(key, "mode") == 0)
        instance->absolute_mode = atoi(value) != 0;
    else if (strcmp(key, "fader_cc") == 0)
        instance->fader_cc = atoi(value);
    else if (strcmp(key, "fader_channel") == 0)
        instance->fader_channel = atoi(value);
    else if (strcmp(key, "hamster") == 0)
        instance->hamster = atoi(value) != 0;
    else if (strcmp(key, "midi_learn") == 0)
        instance->midi_learn = atoi(value) != 0;
    else if (strcmp(key, "sample_file") == 0) {
        atomic_fetch_add_explicit(&instance->path_sequence, 1,
                                  memory_order_acq_rel);
        snprintf(instance->requested_path, sizeof(instance->requested_path),
                 "%s", value);
        atomic_fetch_add_explicit(&instance->path_sequence, 1,
                                  memory_order_release);
    }
    else if (strcmp(key, "cut_in") == 0)
        instance->cut_in = fminf(0.95f, fmaxf(0.0f, strtof(value, NULL)));
    else if (strcmp(key, "curve") == 0)
        instance->curve = fminf(4.0f, fmaxf(0.1f, strtof(value, NULL)));
    else if (strcmp(key, "low_cut") == 0) {
        instance->low_cut_hz = fminf(300.0f, fmaxf(0.0f, strtof(value, NULL)));
        scratch_engine_set_low_cut(&instance->engine, instance->low_cut_hz);
    }
    else if (strcmp(key, "retrigger_ms") == 0)
        instance->retrigger_ms = fminf(30.0f, fmaxf(0.0f, strtof(value, NULL)));
    else if (strcmp(key, "wave_zoom") == 0)
        instance->wave_zoom = atoi(value) < 0 ? 0 : (atoi(value) > 3 ? 3 : atoi(value));
    else if (strcmp(key, "control_mode") == 0) {
        int mode = atoi(value);
        instance->control_mode = mode < 0 ? 0 : (mode > 2 ? 2 : mode);
        instance->knob_touched = 0;
        instance->virtual_samples_remaining = 0;
        if (instance->control_mode == 0) {
            scratch_engine_jog(&instance->engine, 0.0, 0);
        } else {
            scratch_engine_set_rate(&instance->engine,
                                    instance->virtual_play ? instance->virtual_speed : 0.0);
        }
    }
    else if (strcmp(key, "jog_active") == 0) {
        instance->jog_active = atoi(value) != 0;
        if (instance->jog_active) {
            instance->jog_gate_open = 0;
            if (instance->control_mode != 0)
                scratch_engine_jog(&instance->engine,
                                   instance->virtual_play ? instance->virtual_speed : 0.0,
                                   0);
        } else {
            scratch_engine_jog(&instance->engine,
                               instance->control_mode != 0 && instance->virtual_play
                                   ? instance->virtual_speed : 0.0,
                               0);
        }
    }
    else if (strcmp(key, "jog_delta") == 0) {
        double delta = strtod(value, NULL);
        if (instance->jog_active && instance->control_mode != 0)
            scratch_engine_jog(&instance->engine,
                               delta * instance->jog_sensitivity,
                               0);
        if (instance->jog_active && instance->control_mode != 0)
            instance->virtual_samples_remaining = 5292; /* 120 ms release. */
    }
    else if (strcmp(key, "jog_rate") == 0) {
        /* Knob motion crosses the UI's fire-and-forget mailbox, while the
         * touch edge uses an acknowledged write. A final queued motion update
         * can therefore arrive after release. Once Knob mode says the finger
         * is up, discard that stale rate instead of restarting a 50 ms scratch
         * timeout after the motor has already resumed. Jog mode has no touch
         * gate and continues to accept every rate update. */
        if (instance->jog_active && instance->control_mode != 0 &&
            (instance->control_mode != 1 || !instance->jog_touch || instance->knob_touched)) {
            scratch_engine_set_rate_smooth(&instance->engine,
                strtod(value, NULL),
                (unsigned int)lrintf(instance->knob_smoothing_ms * 44.1f));
            instance->virtual_samples_remaining = 2205; /* 50 ms after last detent. */
        }
    }
    else if (strcmp(key, "knob_touch") == 0) {
        if (instance->jog_active &&
            ((instance->control_mode == 1 && instance->jog_touch) ||
             (instance->control_mode == 2 && instance->jog_touch))) {
            instance->knob_touched = atoi(value) != 0;
            if (!instance->knob_touched)
                instance->virtual_samples_remaining = 0;
            scratch_engine_set_rate_smooth(&instance->engine,
                instance->knob_touched ? 0.0 :
                (instance->virtual_play ? instance->virtual_speed : 0.0),
                (unsigned int)lrintf(instance->touch_inertia_ms * 44.1f));
        }
    }
    else if (strcmp(key, "jog_gate") == 0) {
        int command = atoi(value);
        instance->jog_gate_open = command != 0;
        if (instance->jog_active && command > 1)
            scratch_engine_retrigger(&instance->engine,
                (unsigned int)lrintf(instance->retrigger_ms * 44.1f));
    }
    else if (strcmp(key, "jog_sensitivity") == 0)
        instance->jog_sensitivity = fminf(2.0f, fmaxf(0.1f, strtof(value, NULL)));
    else if (strcmp(key, "jog_touch") == 0)
        instance->jog_touch = atoi(value) != 0;
    else if (strcmp(key, "knob_sensitivity") == 0)
        instance->knob_sensitivity = fminf(1.0f, fmaxf(0.02f, strtof(value, NULL)));
    else if (strcmp(key, "knob_smoothing_ms") == 0)
        instance->knob_smoothing_ms = fminf(120.0f, fmaxf(0.0f, strtof(value, NULL)));
    else if (strcmp(key, "virtual_play") == 0) {
        instance->virtual_play = atoi(value) != 0;
        if (instance->control_mode != 0 &&
            !instance->knob_touched && instance->virtual_samples_remaining == 0)
            scratch_engine_set_rate(&instance->engine,
                                    instance->virtual_play ? instance->virtual_speed : 0.0);
    }
    else if (strcmp(key, "virtual_speed") == 0) {
        instance->virtual_speed = fminf(2.0f, fmaxf(0.1f, strtof(value, NULL)));
        if (instance->control_mode != 0 &&
            instance->virtual_play && !instance->knob_touched &&
            instance->virtual_samples_remaining == 0)
            scratch_engine_set_rate(&instance->engine, instance->virtual_speed);
    }
    else if (strcmp(key, "touch_inertia_ms") == 0)
        instance->touch_inertia_ms = fminf(250.0f, fmaxf(0.0f, strtof(value, NULL)));
    else if (strcmp(key, "loop") == 0) {
        instance->loop = atoi(value) != 0;
        scratch_engine_set_loop(&instance->engine, instance->loop);
    }
    else if (strcmp(key, "platter") == 0) {
        float next = fminf(1.0f, fmaxf(0.0f, strtof(value, NULL)));
        if (instance->platter_initialized && instance->control_mode == 1) {
            float delta = next - instance->platter_value;
            scratch_engine_jog(&instance->engine,
                               delta * 50.0f * instance->jog_sensitivity,
                               0);
            instance->virtual_samples_remaining = 5292;
        }
        instance->platter_value = next;
        instance->platter_initialized = 1;
    }
    apply_gate(instance);
}

static int scratch_get_param(void *opaque, const char *key, char *out, int out_len)
{
    scratch_instance_t *instance = opaque;
    int value;
    if (!instance || !key || !out || out_len <= 0)
        return -1;
    if (strcmp(key, "chain_params") == 0) {
        static const char contract[] =
            "["
            "{\"key\":\"sample_file\",\"name\":\"Sample\",\"type\":\"filepath\","
              "\"root\":\"/data/UserData/UserLibrary/Samples\","
              "\"start_path\":\"/data/UserData/UserLibrary/Samples\","
              "\"filter\":\".wav\",\"default\":\"\"},"
            "{\"key\":\"control_mode\",\"name\":\"Control\",\"type\":\"enum\","
              "\"options\":[\"DVS\",\"Knob\",\"Jog\"],\"default\":0},"
            "{\"key\":\"mode\",\"name\":\"Tracking\",\"type\":\"enum\","
              "\"options\":[\"Relative\",\"Absolute\"],\"default\":0},"
            "{\"key\":\"fader_cc\",\"name\":\"Fader CC\",\"type\":\"int\","
              "\"min\":0,\"max\":127,\"step\":1,\"default\":1},"
            "{\"key\":\"fader_channel\",\"name\":\"MIDI Ch\",\"type\":\"int\","
              "\"min\":0,\"max\":16,\"step\":1,\"default\":16},"
            "{\"key\":\"midi_learn\",\"name\":\"Learn CC\",\"type\":\"enum\","
              "\"options\":[\"Off\",\"Armed\"],\"default\":0},"
            "{\"key\":\"hamster\",\"name\":\"Hamster\",\"type\":\"enum\","
              "\"options\":[\"Off\",\"On\"],\"default\":0},"
            "{\"key\":\"cut_in\",\"name\":\"Cut In\",\"type\":\"float\","
              "\"min\":0,\"max\":0.95,\"step\":0.01,\"default\":0.03},"
            "{\"key\":\"curve\",\"name\":\"Curve\",\"type\":\"float\","
              "\"min\":0.1,\"max\":4,\"step\":0.1,\"default\":1},"
            "{\"key\":\"low_cut\",\"name\":\"Low Cut\",\"type\":\"float\","
              "\"min\":0,\"max\":300,\"step\":5,\"unit\":\"Hz\",\"default\":100},"
            "{\"key\":\"retrigger_ms\",\"name\":\"Retrigger\",\"type\":\"float\","
              "\"min\":0,\"max\":30,\"step\":0.5,\"unit\":\"ms\",\"default\":8.7},"
            "{\"key\":\"wave_zoom\",\"name\":\"Wave Zoom\",\"type\":\"enum\","
              "\"options\":[\"1 sec\",\"4 sec\",\"16 sec\",\"Overview\"],\"default\":1},"
            "{\"key\":\"jog_sensitivity\",\"name\":\"Jog Feel\",\"type\":\"float\","
              "\"min\":0.1,\"max\":2,\"step\":0.05,\"default\":0.55},"
            "{\"key\":\"jog_touch\",\"name\":\"Jog Touch\",\"type\":\"enum\","
              "\"options\":[\"Off\",\"On\"],\"default\":0},"
            "{\"key\":\"knob_sensitivity\",\"name\":\"Knob Feel\",\"type\":\"float\","
              "\"min\":0.02,\"max\":1,\"step\":0.02,\"default\":0.18},"
            "{\"key\":\"knob_smoothing_ms\",\"name\":\"Knob Smooth\",\"type\":\"float\","
              "\"min\":0,\"max\":120,\"step\":5,\"unit\":\"ms\",\"default\":25},"
            "{\"key\":\"virtual_play\",\"name\":\"Motor\",\"type\":\"enum\","
              "\"options\":[\"Stop\",\"Play\"],\"default\":1},"
            "{\"key\":\"virtual_speed\",\"name\":\"Motor Speed\",\"type\":\"float\","
              "\"min\":0.1,\"max\":2,\"step\":0.05,\"unit\":\"x\",\"default\":1},"
            "{\"key\":\"touch_inertia_ms\",\"name\":\"Deck Inertia\",\"type\":\"float\","
              "\"min\":0,\"max\":250,\"step\":5,\"unit\":\"ms\",\"default\":45},"
            "{\"key\":\"loop\",\"name\":\"Loop\",\"type\":\"enum\","
              "\"options\":[\"Off\",\"On\"],\"default\":1},"
            "{\"key\":\"platter\",\"name\":\"Knob Scratch\",\"type\":\"float\","
              "\"min\":0,\"max\":1,\"step\":0.01,\"default\":0.5},"
            "{\"key\":\"dvs_status\",\"name\":\"DVS Status\",\"type\":\"string\","
              "\"access\":\"read\",\"live\":true},"
            "{\"key\":\"monitor\",\"name\":\"DVS Monitor\",\"type\":\"canvas\","
              "\"canvas_script\":\"monitor.js\",\"as_page\":true,\"show_value\":false,"
              "\"extra_keys\":[\"dvs_status\"]},"
            "{\"key\":\"scratch_view_status\",\"name\":\"Scratch View Status\","
              "\"type\":\"string\",\"access\":\"read\",\"live\":true},"
            "{\"key\":\"scratch_view\",\"name\":\"Scratch View\",\"type\":\"canvas\","
              "\"canvas_script\":\"scratch_view.js\",\"show_value\":false,\"show_footer\":false,\"fullscreen_live_ms\":100,"
              "\"extra_keys\":[\"scratch_view_status\"]}"
            "]";
        value = snprintf(out, out_len, "%s", contract);
    } else if (strcmp(key, "ui_hierarchy") == 0) {
        static const char hierarchy[] =
            "{\"levels\":{\"root\":{\"label\":\"Scratch\","
            "\"params\":[\"sample_file\",\"control_mode\",\"scratch_view\",\"hamster\","
                         "\"loop\",\"low_cut\",\"cut_in\",\"retrigger_ms\",\"monitor\"],"
            "\"knobs\":[\"sample_file\",\"control_mode\",\"scratch_view\",\"hamster\","
                        "\"loop\",\"low_cut\",\"cut_in\",\"retrigger_ms\"]}}}";
        value = snprintf(out, out_len, "%s", hierarchy);
    } else if (strcmp(key, "control_mode") == 0)
        value = snprintf(out, out_len, "%d", instance->control_mode);
    else if (strcmp(key, "mode") == 0)
        value = snprintf(out, out_len, "%d", instance->absolute_mode);
    else if (strcmp(key, "fader_cc") == 0)
        value = snprintf(out, out_len, "%d", instance->fader_cc);
    else if (strcmp(key, "fader_channel") == 0)
        value = snprintf(out, out_len, "%d", instance->fader_channel);
    else if (strcmp(key, "hamster") == 0)
        value = snprintf(out, out_len, "%d", instance->hamster);
    else if (strcmp(key, "midi_learn") == 0)
        value = snprintf(out, out_len, "%d", instance->midi_learn);
    else if (strcmp(key, "low_cut") == 0)
        value = snprintf(out, out_len, "%.1f", instance->low_cut_hz);
    else if (strcmp(key, "retrigger_ms") == 0)
        value = snprintf(out, out_len, "%.1f", instance->retrigger_ms);
    else if (strcmp(key, "wave_zoom") == 0)
        value = snprintf(out, out_len, "%d", instance->wave_zoom);
    else if (strcmp(key, "jog_sensitivity") == 0)
        value = snprintf(out, out_len, "%.2f", instance->jog_sensitivity);
    else if (strcmp(key, "jog_touch") == 0)
        value = snprintf(out, out_len, "%d", instance->jog_touch);
    else if (strcmp(key, "knob_sensitivity") == 0)
        value = snprintf(out, out_len, "%.2f", instance->knob_sensitivity);
    else if (strcmp(key, "knob_smoothing_ms") == 0)
        value = snprintf(out, out_len, "%.1f", instance->knob_smoothing_ms);
    else if (strcmp(key, "virtual_play") == 0)
        value = snprintf(out, out_len, "%d", instance->virtual_play);
    else if (strcmp(key, "virtual_speed") == 0)
        value = snprintf(out, out_len, "%.2f", instance->virtual_speed);
    else if (strcmp(key, "touch_inertia_ms") == 0)
        value = snprintf(out, out_len, "%.1f", instance->touch_inertia_ms);
    else if (strcmp(key, "loop") == 0)
        value = snprintf(out, out_len, "%d", instance->loop);
    else if (strcmp(key, "platter") == 0)
        value = snprintf(out, out_len, "%.2f", instance->platter_value);
    else if (strcmp(key, "jog_scratch") == 0)
        value = snprintf(out, out_len, "%s", "");
    else if (strcmp(key, "last_cc") == 0)
        value = snprintf(out, out_len, "%d", instance->last_cc);
    else if (strcmp(key, "last_value") == 0)
        value = snprintf(out, out_len, "%d", instance->last_value);
    else if (strcmp(key, "sample_file") == 0)
        value = snprintf(out, out_len, "%s", instance->requested_path);
    else if (strcmp(key, "state") == 0) {
        char escaped_path[PATH_MAX * 2];
        if (!json_escape(instance->requested_path, escaped_path, sizeof(escaped_path)))
            return -1;
        value = snprintf(out, out_len,
            "{\"version\":1,\"sample_file\":\"%s\",\"control_mode\":%d,\"mode\":%d,"
            "\"fader_cc\":%d,\"fader_channel\":%d,\"hamster\":%d,"
            "\"cut_in\":%.4f,\"curve\":%.4f,\"low_cut\":%.1f,"
            "\"retrigger_ms\":%.1f,\"wave_zoom\":%d,\"jog_sensitivity\":%.3f,\"jog_touch\":%d,"
            "\"knob_sensitivity\":%.3f,\"knob_smoothing_ms\":%.1f,\"virtual_play\":%d,\"virtual_speed\":%.3f,"
            "\"touch_inertia_ms\":%.1f,\"loop\":%d}",
            escaped_path, instance->control_mode, instance->absolute_mode, instance->fader_cc,
            instance->fader_channel, instance->hamster, instance->cut_in,
            instance->curve, instance->low_cut_hz, instance->retrigger_ms,
            instance->wave_zoom, instance->jog_sensitivity, instance->jog_touch, instance->knob_sensitivity,
            instance->knob_smoothing_ms, instance->virtual_play, instance->virtual_speed,
            instance->touch_inertia_ms, instance->loop);
    }
    else if (strcmp(key, "monitor") == 0 || strcmp(key, "scratch_view") == 0)
        value = snprintf(out, out_len, "%s", "");
    else if (strcmp(key, "scratch_view_status") == 0 ||
             strcmp(key, "scratch_view_status:effective") == 0 ||
             strcmp(key, "scratch_view_status:base") == 0) {
        static const char hex[] = "0123456789abcdef";
        char envelope[129];
        double centre = instance->engine.position_frames;
        static const double zoom_seconds[] = {1.0, 4.0, 16.0};
        double playhead = 64.0;
        int x;
        for (x = 0; x < 128; ++x) {
            unsigned int level = 0;
            if (instance->wave_zoom == 3) {
                if (instance->overview)
                    level = instance->overview[x] >> 4;
            } else if (instance->waveform) {
                double window_frames = 44100.0 * zoom_seconds[instance->wave_zoom];
                double frame_a = centre + ((double)x - 64.0) * window_frames / 128.0;
                double frame_b = centre + ((double)x - 63.0) * window_frames / 128.0;
                if (frame_b > 0.0 && frame_a < (double)instance->engine.sample_frames) {
                    size_t first;
                    size_t last;
                    size_t bin;
                    unsigned char peak = 0;
                    if (frame_a < 0.0) frame_a = 0.0;
                    if (frame_b > (double)instance->engine.sample_frames)
                        frame_b = (double)instance->engine.sample_frames;
                    first = (size_t)(frame_a / WAVEFORM_BIN_FRAMES);
                    last = (size_t)((frame_b + WAVEFORM_BIN_FRAMES - 1) /
                                    WAVEFORM_BIN_FRAMES);
                    if (last <= first) last = first + 1;
                    for (bin = first; bin < last && bin < instance->waveform_bins; ++bin) {
                        if (instance->waveform[bin] > peak)
                            peak = instance->waveform[bin];
                    }
                    level = peak >> 4;
                }
            }
            envelope[x] = hex[level & 0x0f];
        }
        if (instance->wave_zoom == 3 && instance->engine.sample_frames > 1)
            playhead = centre * 127.0 / (double)(instance->engine.sample_frames - 1);
        envelope[128] = '\0';
        value = snprintf(out, out_len, "%.3f,%.4f,%d,%d,%.2f,%s",
                         centre / 44100.0,
                         instance->decoded_pitch * (instance->control_mode == 0
                             ? instance->virtual_speed : 1.0f),
                         instance->decoded_locked, instance->wave_zoom,
                         playhead, envelope);
    }
    else if (strcmp(key, "dvs_status") == 0) {
        static const char hex[] = "0123456789abcdef";
        char scope[33];
        int i;
        for (i = 0; i < 16; ++i) {
            scope[i * 2] = hex[instance->scope_xy[i * 2] & 0x0f];
            scope[i * 2 + 1] = hex[instance->scope_xy[i * 2 + 1] & 0x0f];
        }
        scope[32] = '\0';
        value = snprintf(out, out_len,
                         "%.4f,%.4f,%.4f,%.3f,%.3f,%d,%.2f,%05x,%d,%d,%d,%d,%d,%d,%s,%d,%zu,%.3f,%.3f",
                         instance->input_peak_l, instance->input_peak_r,
                         instance->decoded_pitch,
                         instance->decoded_position_seconds,
                         instance->decoded_quality, instance->decoded_locked,
                         instance->decoded_age_ms, instance->decoded_word & 0xfffff,
                         instance->last_cc, instance->last_value,
                         instance->pad_gate_active,
                         !!(instance->held_notes[0] || instance->held_notes[1] ||
                            instance->held_notes[2] || instance->held_notes[3]),
                         instance->last_note, instance->last_note_source,
                         scope, atomic_load(&instance->loader_state),
                         instance->engine.sample_frames,
                         instance->engine.position_frames / 44100.0,
                         instance->engine.fader);
    }
    else if (strcmp(key, "cut_in") == 0)
        value = snprintf(out, out_len, "%.3f", instance->cut_in);
    else if (strcmp(key, "curve") == 0)
        value = snprintf(out, out_len, "%.3f", instance->curve);
    else if (strcmp(key, "load_status") == 0)
        value = snprintf(out, out_len, "%d", atomic_load(&instance->loader_state));
    else
        return -1;
    return value >= 0 && value < out_len ? value : -1;
}

static int scratch_get_error(void *opaque, char *out, int out_len)
{
    scratch_instance_t *instance = opaque;
    if (instance && atomic_load(&instance->loader_state) < 0)
        return snprintf(out, out_len, "Selected file must be a 44.1kHz 16-bit stereo PCM WAV");
    if (out_len > 0)
        out[0] = '\0';
    return 0;
}

static void scratch_render(void *opaque, int16_t *out_lr, int frames)
{
    scratch_instance_t *instance = opaque;
    int16_t *audio_in;
    int16_t decoder_in[MOVE_FRAMES_PER_BLOCK * 2];
    double when = 0.0;
    int position;
    double pitch;
    unsigned int generation;
    float peak_l = 0.0f;
    float peak_r = 0.0f;
    int i;

    if (!instance || !g_host || !g_host->mapped_memory) {
        memset(out_lr, 0, (size_t)frames * 2 * sizeof(*out_lr));
        return;
    }

    generation = atomic_load_explicit(&instance->published_generation,
                                      memory_order_acquire);
    if (generation != instance->local_generation) {
        int16_t *sample = atomic_load_explicit(&instance->published_sample,
                                               memory_order_relaxed);
        size_t sample_frames = atomic_load_explicit(&instance->published_frames,
                                                    memory_order_relaxed);
        scratch_engine_set_sample(&instance->engine, sample, sample_frames);
        instance->waveform = atomic_load_explicit(&instance->published_waveform,
                                                  memory_order_relaxed);
        instance->waveform_bins = atomic_load_explicit(
            &instance->published_waveform_bins, memory_order_relaxed);
        instance->overview = atomic_load_explicit(&instance->published_overview,
                                                  memory_order_relaxed);
        instance->local_generation = generation;
        atomic_store_explicit(&instance->adopted_generation, generation,
                              memory_order_release);
    }

    if (atomic_load_explicit(&instance->decoder_ready, memory_order_acquire) != 1) {
        memset(out_lr, 0, (size_t)frames * 2 * sizeof(*out_lr));
        return;
    }

    audio_in = (int16_t *)(g_host->mapped_memory + g_host->audio_in_offset);
    for (i = 0; i < frames; ++i) {
        float l = fabsf(audio_in[i * 2] / 32768.0f);
        float r = fabsf(audio_in[i * 2 + 1] / 32768.0f);
        if (l > peak_l) peak_l = l;
        if (r > peak_r) peak_r = r;
    }
    instance->input_peak_l = fmaxf(peak_l, instance->input_peak_l * 0.92f);
    instance->input_peak_r = fmaxf(peak_r, instance->input_peak_r * 0.92f);
    for (i = 0; i < 16; ++i) {
        int frame = i * frames / 16;
        int x = (audio_in[frame * 2] + 32768) * 15 / 65535;
        int y = (audio_in[frame * 2 + 1] + 32768) * 15 / 65535;
        instance->scope_xy[i * 2] = (unsigned char)x;
        instance->scope_xy[i * 2 + 1] = (unsigned char)y;
    }
    /* OMNI's RCA line path presents the Serato quadrature channels in the
     * opposite orientation to xwax's conventional interface ordering.  Swap
     * only the decoder feed: meters still describe the physical Move input,
     * while forward platter motion produces positive playback pitch. */
    for (i = 0; i < frames; ++i) {
        decoder_in[i * 2] = audio_in[i * 2 + 1];
        decoder_in[i * 2 + 1] = audio_in[i * 2];
    }
    timecoder_submit(&instance->decoder, decoder_in, (size_t)frames);
    pitch = timecoder_get_pitch(&instance->decoder);
    position = timecoder_get_position(&instance->decoder, &when);
    instance->decoded_pitch = pitch;
    instance->decoded_locked = position >= 0;
    instance->decoded_position_seconds = position >= 0
        ? position / timecoder_get_resolution(&instance->decoder) + pitch * when
        : 0.0;
    instance->decoded_age_ms = when * 1000.0;
    instance->decoded_quality = fminf(1.0f, instance->decoder.valid_counter / 48.0f);
    instance->decoded_word = instance->decoder.bitstream;
    if (instance->control_mode == 0)
        scratch_engine_follow_timecode(
            &instance->engine, pitch * instance->virtual_speed,
            instance->decoded_position_seconds,
            position >= 0, instance->absolute_mode != 0);
    if (atomic_load_explicit(&instance->loader_state, memory_order_acquire) == 1)
        scratch_engine_render(&instance->engine, out_lr, frames);
    else
        memset(out_lr, 0, (size_t)frames * 2 * sizeof(*out_lr));

    /* Schwung's slot scheduler parks a sound generator after one second of
     * silence; requires_continuous_processing currently applies only to FX.
     * A parked DVS decoder cannot see enough consecutive control-vinyl blocks
     * to lock again until MIDI wakes it. Keep one rotating sample just above
     * the host's +/-4 idle threshold. Its block RMS is below -97 dBFS. */
    {
        int audible = 0;
        for (i = 0; i < frames * 2; ++i) {
            if (out_lr[i] > 4 || out_lr[i] < -4) {
                audible = 1;
                break;
            }
        }
        if (!audible && frames > 0) {
            unsigned int sample = instance->keepalive_phase++ % (unsigned int)frames;
            out_lr[sample * 2] = (instance->keepalive_phase & 1u) ? 5 : -5;
            out_lr[sample * 2 + 1] = -out_lr[sample * 2];
        }
    }
    if (instance->virtual_samples_remaining > (unsigned int)frames) {
        instance->virtual_samples_remaining -= (unsigned int)frames;
    } else if (instance->virtual_samples_remaining > 0) {
        instance->virtual_samples_remaining = 0;
        if (instance->control_mode != 0)
            scratch_engine_set_rate_smooth(&instance->engine,
                instance->knob_touched ? 0.0 :
                (instance->virtual_play ? instance->virtual_speed : 0.0),
                (unsigned int)lrintf(instance->touch_inertia_ms * 44.1f));
    }
}

static plugin_api_v2_t g_api = {
    .api_version = MOVE_PLUGIN_API_VERSION_2,
    .create_instance = scratch_create,
    .destroy_instance = scratch_destroy,
    .on_midi = scratch_on_midi,
    .set_param = scratch_set_param,
    .get_param = scratch_get_param,
    .get_error = scratch_get_error,
    .render_block = scratch_render,
};

plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host)
{
    g_host = host;
    return &g_api;
}
