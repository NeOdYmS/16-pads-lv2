#define _POSIX_C_SOURCE 199309L
#include "sampler.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Worst-case render cost: 16 stereo pads playing at once. */
static double seconds(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
}

static double bench(uint32_t sample_rate, double host_rate, uint32_t block)
{
    const uint32_t frames = 10 * sample_rate;
    Sample sample = {malloc((size_t)frames * 2 * sizeof(float)), frames,
                     sample_rate, 2};
    for (uint32_t i = 0; i < frames * 2; ++i) {
        sample.data[i] = (float)((i * 2654435761u) >> 8) / 16777216.0f - 0.5f;
    }
    Pad pads[PAD_COUNT];
    memset(pads, 0, sizeof(pads));
    float left[1024], right[1024];
    const uint32_t blocks = (uint32_t)(host_rate * 20) / block;
    const double begin = seconds();
    for (uint32_t b = 0; b < blocks; ++b) {
        memset(left, 0, block * sizeof(float));
        memset(right, 0, block * sizeof(float));
        for (unsigned p = 0; p < PAD_COUNT; ++p) {
            if (!pads[p].playing) {
                pads[p].sample = &sample;
                pad_trigger(&pads[p], 1);
            }
            pad_render(&pads[p], PAD_LOOP, 0.5f, host_rate, left, right, 0, block);
        }
    }
    const double elapsed = seconds() - begin;
    free(sample.data);
    if (left[0] == 12345.0f) {
        puts("");
    }
    return 100.0 * elapsed / (blocks * block / host_rate);
}

int main(void)
{
    static const uint32_t blocks[] = {64, 128, 256};
    for (unsigned i = 0; i < 3; ++i) {
        printf("block %3u: native 48k %.2f%% DSP, resampled 44.1k->48k %.2f%% DSP\n",
               blocks[i], bench(48000, 48000, blocks[i]),
               bench(44100, 48000, blocks[i]));
    }
    return 0;
}
