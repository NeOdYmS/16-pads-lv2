#include "sampler.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t u16(const unsigned char* p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t u32(const unsigned char* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static Sample* fail(FILE* file, Sample* sample, char* error, size_t size,
                    const char* message)
{
    if (file) {
        fclose(file);
    }
    sample_free(sample);
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return NULL;
}

Sample* sample_load(const char* path, char* error, size_t error_size)
{
    FILE* file = fopen(path, "rb");
    Sample* sample = NULL;
    unsigned char header[12];
    unsigned char fmt[16];
    uint16_t format = 0, channels = 0, bits = 0, alignment = 0;
    uint32_t rate = 0, data_size = 0;
    long data_offset = 0;
    int have_fmt = 0, have_data = 0;
    if (!file) {
        return fail(NULL, NULL, error, error_size, "Cannot open WAV file");
    }
    if (fread(header, 1, 12, file) != 12 ||
        memcmp(header, "RIFF", 4) || memcmp(header + 8, "WAVE", 4)) {
        return fail(file, NULL, error, error_size, "Expected little-endian RIFF WAV");
    }
    if (fseek(file, 0, SEEK_END)) {
        return fail(file, NULL, error, error_size, "Cannot seek WAV file");
    }
    const long file_size = ftell(file);
    const uint64_t riff_end = (uint64_t)u32(header + 4) + 8;
    if (file_size < 12 || riff_end < 12 || riff_end > (uint64_t)file_size ||
        fseek(file, 12, SEEK_SET)) {
        return fail(file, NULL, error, error_size, "Truncated WAV container");
    }
    while ((uint64_t)ftell(file) + 8 <= riff_end) {
        unsigned char chunk[8];
        if (fread(chunk, 1, 8, file) != 8) {
            return fail(file, NULL, error, error_size, "Truncated WAV chunk");
        }
        const uint32_t size = u32(chunk + 4);
        const long offset = ftell(file);
        const uint64_t next = (uint64_t)offset + size + (size & 1u);
        if (next > riff_end) {
            return fail(file, NULL, error, error_size, "Invalid WAV chunk size");
        }
        if (!memcmp(chunk, "fmt ", 4) && !have_fmt) {
            if (size < 16 || fread(fmt, 1, 16, file) != 16) {
                return fail(file, NULL, error, error_size, "Invalid WAV format");
            }
            format = u16(fmt);
            channels = u16(fmt + 2);
            rate = u32(fmt + 4);
            alignment = u16(fmt + 12);
            bits = u16(fmt + 14);
            have_fmt = 1;
        } else if (!memcmp(chunk, "data", 4) && !have_data) {
            data_offset = offset;
            data_size = size;
            have_data = 1;
        }
        if (fseek(file, (long)next, SEEK_SET)) {
            return fail(file, NULL, error, error_size, "Cannot seek WAV chunk");
        }
    }
    if (!have_fmt || !have_data || !rate || rate > 768000 ||
        (channels != 1 && channels != 2) ||
        !((format == 1 && (bits == 16 || bits == 24 || bits == 32)) ||
          (format == 3 && bits == 32)) ||
        alignment != channels * (bits / 8) || !data_size ||
        data_size % alignment) {
        return fail(file, NULL, error, error_size,
                    "Supported WAV: mono/stereo PCM 16/24/32-bit or float32");
    }
    const uint32_t frames = data_size / alignment;
    if ((uint64_t)frames * channels * sizeof(float) > SAMPLE_MAX_BYTES) {
        return fail(file, NULL, error, error_size, "Decoded WAV exceeds 64 MiB");
    }
    sample = calloc(1, sizeof(*sample));
    if (!sample) {
        return fail(file, NULL, error, error_size, "Cannot allocate sample");
    }
    sample->frames = frames;
    sample->channels = channels;
    sample->rate = rate;
    sample->data = malloc((size_t)frames * channels * sizeof(float));
    if (!sample->data || fseek(file, data_offset, SEEK_SET)) {
        return fail(file, sample, error, error_size, "Cannot allocate/read sample");
    }
    for (size_t i = 0; i < (size_t)frames * channels; ++i) {
        unsigned char bytes[4] = {0};
        if (fread(bytes, 1, bits / 8, file) != (size_t)(bits / 8)) {
            return fail(file, sample, error, error_size, "Truncated WAV data");
        }
        float value;
        if (format == 3) {
            const uint32_t word = u32(bytes);
            memcpy(&value, &word, sizeof(value));
            if (!isfinite(value)) {
                return fail(file, sample, error, error_size, "Non-finite WAV audio");
            }
            value = fminf(1.0f, fmaxf(-1.0f, value));
        } else {
            const uint32_t word = u32(bytes);
            const unsigned width = bits;
            const double signed_value = (word & (1u << (width - 1))) ?
                (double)word - ldexp(1.0, (int)width) : (double)word;
            value = (float)(signed_value / ldexp(1.0, (int)width - 1));
        }
        sample->data[i] = value;
    }
    fclose(file);
    return sample;
}

void sample_free(Sample* sample)
{
    if (sample) {
        free(sample->data);
        free(sample);
    }
}

int midi_assignment(float value)
{
    return isfinite(value) && value >= 0.0f && value <= 127.0f ?
           (int)lroundf(value) : -1;
}

PadMode pad_mode(float value)
{
    return !isfinite(value) || value < 0.5f ? PAD_ONE_SHOT :
           value < 1.5f ? PAD_LOOP : PAD_TRIG;
}

float pad_gain(float value)
{
    return isfinite(value) ?
           powf(10.0f, fminf(12.0f, fmaxf(-60.0f, value)) / 20.0f) : 0.0f;
}

void pad_stop(Pad* pad)
{
    pad->playing = 0;
}

int pad_active(const Pad* pad)
{
    return pad->playing;
}

void pad_trigger(Pad* pad, float velocity)
{
    if (pad->playing) {
        pad_stop(pad);
        return;
    }
    if (!pad->sample) {
        return;
    }
    pad->position = 0;
    pad->velocity = velocity;
    pad->playing = 1;
}

void pad_release(Pad* pad, PadMode mode)
{
    if (mode == PAD_TRIG && !pad->note_held && !pad->cc_held && !pad->gate_held) {
        pad_stop(pad);
    }
}

void pad_render(Pad* pad, PadMode mode, float gain, double host_rate,
                float* left, float* right, uint32_t start, uint32_t end)
{
    const Sample* sample = pad->sample;
    if (!sample) {
        return;
    }
    const double step = sample->rate / host_rate;
    for (uint32_t frame = start; frame < end && pad->playing; ++frame) {
        if (pad->position >= sample->frames) {
            if (mode == PAD_LOOP) {
                pad->position = fmod(pad->position, sample->frames);
            } else {
                pad->playing = 0;
                break;
            }
        }
        const uint32_t index = (uint32_t)pad->position;
        const uint32_t next = index + 1 < sample->frames ? index + 1 :
                              mode == PAD_LOOP ? 0 : index;
        const float fraction = (float)(pad->position - index);
        const float amplitude = gain * pad->velocity;
        for (unsigned channel = 0; channel < 2; ++channel) {
            const unsigned c = sample->channels == 1 ? 0 : channel;
            const float a = sample->data[index * sample->channels + c];
            const float b = sample->data[next * sample->channels + c];
            (channel ? right : left)[frame] += (a + (b - a) * fraction) * amplitude;
        }
        pad->position += step;
    }
    if (mode != PAD_LOOP && pad->position >= sample->frames) {
        pad->playing = 0;
    }
}
