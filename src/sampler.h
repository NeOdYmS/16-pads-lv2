#ifndef SAMPLER_H
#define SAMPLER_H

#include <stddef.h>
#include <stdint.h>

#define PAD_COUNT 16
#define SAMPLE_MAX_BYTES (64u * 1024u * 1024u)
#define SAMPLE_SILENCE (1.0f / 4096.0f)

typedef enum { PAD_ONE_SHOT, PAD_LOOP, PAD_TRIG } PadMode;

typedef struct {
    float* data;
    uint32_t frames;
    uint32_t rate;
    uint16_t channels;
} Sample;

typedef struct {
    Sample* sample;
    double position;
    float velocity;
    int playing;
    int gate_held;
} Pad;

Sample* sample_load(const char* path, char* error, size_t error_size);
void sample_free(Sample* sample);
void pad_trigger(Pad* pad, float velocity);
void pad_stop(Pad* pad);
int pad_active(const Pad* pad);
void pad_release(Pad* pad, PadMode mode);
void pad_render(Pad* pad, PadMode mode, float gain, double host_rate,
                float* left, float* right, uint32_t start, uint32_t end);
PadMode pad_mode(float value);
float pad_gain(float value);

#endif
