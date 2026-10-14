#include "sampler.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void near(float actual, float expected)
{
    assert(fabsf(actual - expected) < 0.0001f);
}

static void playback(void)
{
    float audio[] = {0.25f, 0.5f, 0.75f};
    Sample sample = {audio, 3, 48000, 1};
    Pad pad = {0};
    pad.sample = &sample;
    float left[8] = {0}, right[8] = {0};
    pad_trigger(&pad, 0.5f);
    pad_render(&pad, PAD_ONE_SHOT, 2, 48000, left, right, 0, 8);
    near(left[0], 0.25f);
    near(left[2], 0.75f);
    near(left[3], 0);
    near(right[1], left[1]);
    assert(!pad_active(&pad));

    memset(left, 0, sizeof(left));
    pad_trigger(&pad, 1);
    pad_render(&pad, PAD_LOOP, 1, 48000, left, right, 0, 8);
    near(left[3], 0.25f);
    near(left[7], 0.5f);
    assert(pad_active(&pad) == 1);
    pad_release(&pad, PAD_LOOP);
    assert(pad_active(&pad) == 1);
    pad_trigger(&pad, 1);
    assert(!pad_active(&pad));
    pad_trigger(&pad, 1);
    near((float)pad.position, 0);
    assert(pad_active(&pad) == 1);

    pad.gate_held = 1;
    pad_release(&pad, PAD_TRIG);
    assert(pad_active(&pad));
    pad.gate_held = 0;
    pad_release(&pad, PAD_TRIG);
    assert(!pad_active(&pad));
    pad_trigger(&pad, 1);
    pad_render(&pad, PAD_TRIG, 1, 48000, left, right, 0, 8);
    assert(!pad_active(&pad));

    memset(left, 0, sizeof(left));
    pad_trigger(&pad, 1);
    pad_render(&pad, PAD_ONE_SHOT, 1, 96000, left, right, 0, 8);
    near(left[1], 0.375f);
    near(left[3], 0.625f);
    near(left[5], 0.75f);
    near(left[6], 0);

    float stereo[] = {0.25f, -0.25f, 0.5f, -0.5f};
    Sample two = {stereo, 2, 48000, 2};
    pad.sample = &two;
    memset(left, 0, sizeof(left));
    memset(right, 0, sizeof(right));
    pad_trigger(&pad, 1);
    pad_render(&pad, PAD_ONE_SHOT, 1, 48000, left, right, 2, 6);
    near(left[0], 0);
    near(left[2], 0.25f);
    near(right[3], -0.5f);

    pad.sample = NULL;
    pad_trigger(&pad, 1);
    assert(!pad_active(&pad));
}

static void stop_on_retrigger(void)
{
    float audio[] = {0.5f, 0.5f, 0.5f, 0.5f};
    Sample sample = {audio, 4, 48000, 1};
    Pad pad = {0};
    pad.sample = &sample;
    float left[8] = {0}, right[8] = {0};
    pad_trigger(&pad, 1);
    pad_render(&pad, PAD_ONE_SHOT, 1, 48000, left, right, 0, 2);
    pad_trigger(&pad, 1);
    assert(!pad_active(&pad));
    pad_render(&pad, PAD_ONE_SHOT, 1, 48000, left, right, 2, 4);
    pad_trigger(&pad, 1);
    near((float)pad.position, 0);
    pad_render(&pad, PAD_ONE_SHOT, 1, 48000, left, right, 4, 8);
    near(left[0], 0.5f);
    near(left[2], 0);
    near(left[4], 0.5f);
    near(left[6], 0.5f);
    for (unsigned i = 0; i < 20; ++i) {
        pad_trigger(&pad, 1);
        assert(pad_active(&pad) == (i % 2 == 0));
        near((float)pad.position, 0);
    }
    pad_trigger(&pad, 1);
    assert(pad_active(&pad) == 1);
    pad_stop(&pad);
    pad.gate_held = 1;
    pad_trigger(&pad, 1);
    pad_trigger(&pad, 1);
    assert(!pad_active(&pad));
    pad_trigger(&pad, 1);
    assert(pad_active(&pad) == 1);
    pad_release(&pad, PAD_TRIG);
    assert(pad_active(&pad) == 1);
    pad.gate_held = 0;
    pad_release(&pad, PAD_TRIG);
    assert(!pad_active(&pad));
}

static void write_bytes(FILE* file, uint32_t value, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        assert(fputc((int)((value >> (8 * i)) & 255), file) != EOF);
    }
}

static void wav(const char* path, unsigned format, unsigned bits, uint32_t word)
{
    FILE* file = fopen(path, "wb");
    assert(file);
    const unsigned bytes = bits / 8;
    assert(fwrite("RIFF", 1, 4, file) == 4);
    write_bytes(file, 36 + bytes + (bytes & 1), 4);
    assert(fwrite("WAVEfmt ", 1, 8, file) == 8);
    write_bytes(file, 16, 4);
    write_bytes(file, format, 2);
    write_bytes(file, 1, 2);
    write_bytes(file, 48000, 4);
    write_bytes(file, 48000 * bytes, 4);
    write_bytes(file, bytes, 2);
    write_bytes(file, bits, 2);
    assert(fwrite("data", 1, 4, file) == 4);
    write_bytes(file, bytes, 4);
    write_bytes(file, word, bytes);
    if (bytes & 1) {
        write_bytes(file, 0, 1);
    }
    assert(!fclose(file));
}

static void decoder(void)
{
    const char* path = "build/test-sampler.wav";
    char error[160];
    const unsigned bits[] = {16, 24, 32};
    for (unsigned i = 0; i < 3; ++i) {
        wav(path, 1, bits[i], 1u << (bits[i] - 1));
        Sample* sample = sample_load(path, error, sizeof(error));
        assert(sample && sample->frames == 1 && sample->rate == 48000);
        near(sample->data[0], -1);
        sample_free(sample);
    }
    wav(path, 3, 32, 0x3f000000);
    Sample* sample = sample_load(path, error, sizeof(error));
    assert(sample);
    near(sample->data[0], 0.5f);
    sample_free(sample);
    wav(path, 3, 32, 0x7fc00000);
    assert(!sample_load(path, error, sizeof(error)));
    assert(strstr(error, "Non-finite"));
    wav(path, 1, 8, 127);
    assert(!sample_load(path, error, sizeof(error)));
    FILE* file = fopen(path, "wb");
    assert(file);
    assert(fwrite("RIFF", 1, 4, file) == 4);
    assert(!fclose(file));
    assert(!sample_load(path, error, sizeof(error)));
    assert(!sample_load("build/nonexistent-sample.wav", error, sizeof(error)));
    file = fopen(path, "wb");
    assert(file);
    const uint32_t oversized = SAMPLE_MAX_BYTES + 4;
    assert(fwrite("RIFF", 1, 4, file) == 4);
    write_bytes(file, 36 + oversized, 4);
    assert(fwrite("WAVEfmt ", 1, 8, file) == 8);
    write_bytes(file, 16, 4);
    write_bytes(file, 1, 2);
    write_bytes(file, 1, 2);
    write_bytes(file, 48000, 4);
    write_bytes(file, 192000, 4);
    write_bytes(file, 4, 2);
    write_bytes(file, 32, 2);
    assert(fwrite("data", 1, 4, file) == 4);
    write_bytes(file, oversized, 4);
    /* Sparse fixture validates the decoded limit without allocating 64 MiB. */
    assert(!fseek(file, (long)(44 + oversized - 1), SEEK_SET));
    write_bytes(file, 0, 1);
    assert(!fclose(file));
    assert(!sample_load(path, error, sizeof(error)));
    assert(strstr(error, "64 MiB"));
    assert(!remove(path));
}

int main(void)
{
    playback();
    stop_on_retrigger();
    decoder();
    assert(pad_mode(0) == PAD_ONE_SHOT);
    assert(pad_mode(1) == PAD_LOOP);
    assert(pad_mode(2) == PAD_TRIG);
    near(pad_gain(INFINITY), 0);
    near(pad_gain(200), 3.9810717f);
    near(pad_gain(12), 3.9810717f);
    near(pad_gain(0), 1);
    near(pad_gain(-60), 0.001f);
    near(pad_gain(-100), 0.001f);
    near(pad_gain(-6), 0.5011872f);
    puts("Sampler core tests passed");
    return 0;
}
