#ifndef SCHWUNG_SCRATCH_ENGINE_H
#define SCHWUNG_SCRATCH_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct scratch_engine {
    const int16_t *sample_lr;
    size_t sample_frames;
    unsigned int output_rate;
    unsigned int sample_rate;
    double position_frames;
    double rate;
    float fader;
    float low_cut_hz;
    double highpass_alpha;
    double highpass_prev_in[2];
    double highpass_prev_out[2];
    unsigned int retrigger_samples;
} scratch_engine_t;

void scratch_engine_init(scratch_engine_t *engine,
                         unsigned int output_rate,
                         unsigned int sample_rate);
void scratch_engine_set_sample(scratch_engine_t *engine,
                               const int16_t *sample_lr,
                               size_t sample_frames);
void scratch_engine_set_position(scratch_engine_t *engine, double frame);
void scratch_engine_set_rate(scratch_engine_t *engine, double rate);
void scratch_engine_set_fader(scratch_engine_t *engine, float gain);
void scratch_engine_set_low_cut(scratch_engine_t *engine, float hz);
void scratch_engine_retrigger(scratch_engine_t *engine, unsigned int samples);
void scratch_engine_follow_timecode(scratch_engine_t *engine,
                                    double pitch,
                                    double position_seconds,
                                    bool position_valid,
                                    bool absolute_mode);
void scratch_engine_render(scratch_engine_t *engine,
                           int16_t *out_lr,
                           int frames);

#endif
