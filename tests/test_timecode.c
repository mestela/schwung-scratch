#define _GNU_SOURCE

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "timecoder.h"

#define RATE 44100
#define RESOLUTION 1000
#define BITS 20
#define SEED 0x59017u
#define TAPS 0x361e4u

static unsigned int parity(unsigned int value)
{
    unsigned int result = 0;
    while (value) {
        result ^= value & 1u;
        value >>= 1;
    }
    return result;
}

static unsigned int lfsr_forward(unsigned int code)
{
    unsigned int bit = parity(code & (TAPS | 1u));
    return (code >> 1) | (bit << (BITS - 1));
}

int main(void)
{
    struct timecode_def *definition;
    struct timecoder decoder;
    unsigned int code = SEED;
    int previous_cycle = 0;
    int position;
    double when;
    int sample_index;

    definition = timecoder_find_definition("serato_2a");
    assert(definition);
    timecoder_init(&decoder, definition, 1.0, RATE, false);

    for (sample_index = 0; sample_index < RATE * 2; ++sample_index) {
        double cycle = (double)sample_index * RESOLUTION / RATE;
        int whole_cycle = (int)cycle;
        double angle = cycle * 2.0 * M_PI;
        double x = sin(angle);
        double y = cos(angle);
        double modulation = 1.0 - (-cos(angle) + 1.0) * 0.25 *
                                      ((code & 1u) == 0u);
        int16_t pcm[2];

        x *= modulation;
        y *= modulation;
        pcm[0] = (int16_t)(-y * SHRT_MAX * 0.5);
        pcm[1] = (int16_t)(x * SHRT_MAX * 0.5);
        timecoder_submit(&decoder, pcm, 1);

        if (whole_cycle > previous_cycle) {
            code = lfsr_forward(code);
            previous_cycle = whole_cycle;
        }
    }

    position = timecoder_get_position(&decoder, &when);
    assert(position >= 0);
    assert(timecoder_get_pitch(&decoder) > 0.95);
    assert(timecoder_get_pitch(&decoder) < 1.05);
    printf("timecode: position=%d pitch=%.4f age=%.6f\n", position,
           timecoder_get_pitch(&decoder), when);
    timecoder_clear(&decoder);
    timecoder_free_lookup();
    return 0;
}
