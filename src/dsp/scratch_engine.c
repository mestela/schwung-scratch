#include "scratch_engine.h"

#include <math.h>
#include <string.h>

static double clamp_double(double value, double low, double high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static float clamp_float(float value, float low, float high)
{
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

static int16_t clamp_i16(double value)
{
    if (value > 32767.0)
        return 32767;
    if (value < -32768.0)
        return -32768;
    return (int16_t)lrint(value);
}

void scratch_engine_init(scratch_engine_t *engine,
                         unsigned int output_rate,
                         unsigned int sample_rate)
{
    memset(engine, 0, sizeof(*engine));
    engine->output_rate = output_rate;
    engine->sample_rate = sample_rate;
    engine->fader = 1.0f;
}

void scratch_engine_set_sample(scratch_engine_t *engine,
                               const int16_t *sample_lr,
                               size_t sample_frames)
{
    engine->sample_lr = sample_lr;
    engine->sample_frames = sample_frames;
    if (sample_frames == 0)
        engine->position_frames = 0.0;
    else
        engine->position_frames = clamp_double(
            engine->position_frames, 0.0, (double)(sample_frames - 1));
}

void scratch_engine_set_position(scratch_engine_t *engine, double frame)
{
    if (engine->sample_frames == 0) {
        engine->position_frames = 0.0;
        return;
    }
    engine->position_frames = clamp_double(
        frame, 0.0, (double)(engine->sample_frames - 1));
}

void scratch_engine_set_rate(scratch_engine_t *engine, double rate)
{
    engine->rate = clamp_double(rate, -8.0, 8.0);
}

void scratch_engine_set_fader(scratch_engine_t *engine, float gain)
{
    engine->fader = clamp_float(gain, 0.0f, 1.0f);
}

void scratch_engine_set_low_cut(scratch_engine_t *engine, float hz)
{
    const double pi = 3.14159265358979323846;
    engine->low_cut_hz = clamp_float(hz, 0.0f, 300.0f);
    if (engine->low_cut_hz <= 0.0f) {
        engine->highpass_alpha = 0.0;
        engine->highpass_prev_in[0] = engine->highpass_prev_in[1] = 0.0;
        engine->highpass_prev_out[0] = engine->highpass_prev_out[1] = 0.0;
        return;
    }
    {
        double rc = 1.0 / (2.0 * pi * engine->low_cut_hz);
        double dt = 1.0 / engine->output_rate;
        engine->highpass_alpha = rc / (rc + dt);
    }
}

void scratch_engine_retrigger(scratch_engine_t *engine, unsigned int samples)
{
    engine->retrigger_samples = samples;
}

void scratch_engine_jog(scratch_engine_t *engine, double rate,
                        unsigned int hold_samples)
{
    scratch_engine_set_rate(engine, rate);
    engine->jog_samples_remaining = hold_samples;
}

void scratch_engine_follow_timecode(scratch_engine_t *engine,
                                    double pitch,
                                    double position_seconds,
                                    bool position_valid,
                                    bool absolute_mode)
{
    scratch_engine_set_rate(engine, pitch);
    if (absolute_mode && position_valid)
        scratch_engine_set_position(engine,
                                    position_seconds * engine->sample_rate);
}

void scratch_engine_render(scratch_engine_t *engine,
                           int16_t *out_lr,
                           int frames)
{
    int i;
    double step;

    if (!engine->sample_lr || engine->sample_frames == 0) {
        memset(out_lr, 0, (size_t)frames * 2 * sizeof(*out_lr));
        return;
    }

    step = engine->rate * (double)engine->sample_rate /
           (double)engine->output_rate;

    for (i = 0; i < frames; ++i) {
        size_t a = (size_t)engine->position_frames;
        size_t b = a + 1 < engine->sample_frames ? a + 1 : a;
        double fraction = engine->position_frames - (double)a;
        double left = engine->sample_lr[a * 2] * (1.0 - fraction) +
                      engine->sample_lr[b * 2] * fraction;
        double right = engine->sample_lr[a * 2 + 1] * (1.0 - fraction) +
                       engine->sample_lr[b * 2 + 1] * fraction;

        if (engine->highpass_alpha > 0.0) {
            double filtered_left = engine->highpass_alpha *
                (engine->highpass_prev_out[0] + left - engine->highpass_prev_in[0]);
            double filtered_right = engine->highpass_alpha *
                (engine->highpass_prev_out[1] + right - engine->highpass_prev_in[1]);
            engine->highpass_prev_in[0] = left;
            engine->highpass_prev_in[1] = right;
            engine->highpass_prev_out[0] = filtered_left;
            engine->highpass_prev_out[1] = filtered_right;
            left = filtered_left;
            right = filtered_right;
        }

        {
            float gate = engine->retrigger_samples > 0 ? 0.0f : engine->fader;
            if (engine->retrigger_samples > 0)
                engine->retrigger_samples--;
            out_lr[i * 2] = clamp_i16(left * gate);
            out_lr[i * 2 + 1] = clamp_i16(right * gate);
        }

        engine->position_frames += step;
        if (engine->jog_samples_remaining > 0 &&
            --engine->jog_samples_remaining == 0)
            engine->rate = 0.0;
        if (engine->position_frames < 0.0)
            engine->position_frames = 0.0;
        else if (engine->position_frames >= (double)engine->sample_frames)
            engine->position_frames = (double)(engine->sample_frames - 1);
    }
}
