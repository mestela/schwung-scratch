#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "scratch_engine.h"

static void expect_near(int actual, int expected, int tolerance)
{
    assert(abs(actual - expected) <= tolerance);
}

static void test_forward_and_reverse(void)
{
    int16_t sample[] = {
        0, 0, 1000, -1000, 2000, -2000, 3000, -3000,
        4000, -4000, 5000, -5000, 6000, -6000, 7000, -7000,
    };
    int16_t out[8] = {0};
    scratch_engine_t engine;

    scratch_engine_init(&engine, 8, 8);
    scratch_engine_set_sample(&engine, sample, 8);
    scratch_engine_set_fader(&engine, 1.0f);
    scratch_engine_set_position(&engine, 1.0);
    scratch_engine_set_rate(&engine, 1.0);
    scratch_engine_render(&engine, out, 3);

    expect_near(out[0], 1000, 1);
    expect_near(out[2], 2000, 1);
    expect_near(out[4], 3000, 1);

    scratch_engine_set_position(&engine, 5.0);
    scratch_engine_set_rate(&engine, -1.0);
    scratch_engine_render(&engine, out, 3);

    expect_near(out[0], 5000, 1);
    expect_near(out[2], 4000, 1);
    expect_near(out[4], 3000, 1);
}

static void test_fractional_interpolation(void)
{
    int16_t sample[] = {0, 0, 1000, 2000, 2000, 4000};
    int16_t out[2] = {0};
    scratch_engine_t engine;

    scratch_engine_init(&engine, 44100, 44100);
    scratch_engine_set_sample(&engine, sample, 3);
    scratch_engine_set_fader(&engine, 1.0f);
    scratch_engine_set_position(&engine, 0.5);
    scratch_engine_set_rate(&engine, 0.0);
    scratch_engine_render(&engine, out, 1);

    expect_near(out[0], 500, 1);
    expect_near(out[1], 1000, 1);
}

static void test_fader_cut(void)
{
    int16_t sample[] = {12000, -12000, 12000, -12000};
    int16_t out[2] = {1, 1};
    scratch_engine_t engine;

    scratch_engine_init(&engine, 44100, 44100);
    scratch_engine_set_sample(&engine, sample, 2);
    scratch_engine_set_rate(&engine, 0.0);
    scratch_engine_set_fader(&engine, 0.0f);
    scratch_engine_render(&engine, out, 1);

    assert(out[0] == 0);
    assert(out[1] == 0);
}

static void test_absolute_position(void)
{
    int16_t sample[40] = {0};
    scratch_engine_t engine;

    scratch_engine_init(&engine, 10, 10);
    scratch_engine_set_sample(&engine, sample, 20);
    scratch_engine_follow_timecode(&engine, 1.0, 1.25, true, true);

    assert(fabs(engine.position_frames - 12.5) < 0.0001);
    assert(fabs(engine.rate - 1.0) < 0.0001);
}

static void test_pad_retrigger_cut(void)
{
    int16_t sample[] = {1000, -1000};
    int16_t out[8] = {0};
    scratch_engine_t engine;

    scratch_engine_init(&engine, 44100, 44100);
    scratch_engine_set_sample(&engine, sample, 1);
    scratch_engine_set_fader(&engine, 1.0f);
    scratch_engine_retrigger(&engine, 2);
    scratch_engine_render(&engine, out, 4);

    assert(out[0] == 0 && out[2] == 0);
    assert(out[4] == 1000 && out[6] == 1000);
}

static void test_low_cut_removes_dc(void)
{
    int16_t sample[] = {12000, 12000};
    int16_t out[128] = {0};
    scratch_engine_t engine;

    scratch_engine_init(&engine, 1000, 1000);
    scratch_engine_set_sample(&engine, sample, 1);
    scratch_engine_set_fader(&engine, 1.0f);
    scratch_engine_set_low_cut(&engine, 100.0f);
    scratch_engine_render(&engine, out, 64);

    assert(abs(out[0]) > 1000);
    assert(abs(out[126]) < 10);
}

static void test_jog_stops_after_hold(void)
{
    int16_t sample[400] = {0};
    int16_t out[40] = {0};
    scratch_engine_t engine;

    scratch_engine_init(&engine, 100, 100);
    scratch_engine_set_sample(&engine, sample, 200);
    scratch_engine_set_position(&engine, 100.0);
    scratch_engine_jog(&engine, -2.0, 10);
    scratch_engine_render(&engine, out, 10);

    assert(fabs(engine.position_frames - 80.0) < 0.0001);
    assert(fabs(engine.rate) < 0.0001);
    scratch_engine_render(&engine, out, 10);
    assert(fabs(engine.position_frames - 80.0) < 0.0001);
}

int main(void)
{
    test_forward_and_reverse();
    test_fractional_interpolation();
    test_fader_cut();
    test_absolute_position();
    test_pad_retrigger_cut();
    test_low_cut_removes_dc();
    test_jog_stops_after_hold();
    puts("scratch_engine: all tests passed");
    return 0;
}
