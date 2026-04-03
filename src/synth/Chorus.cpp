// Chorus.cpp
#include "Chorus.hpp"
#include <cstring>

static inline int16_t clamp_i16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

Chorus::Chorus()
    : write_idx(0), delay_samples(220), mix_q15(8192) {
    buf.fill(0);
}

Chorus::~Chorus() {}

void Chorus::set_params(int rate_unused, int depth, int mix) {
    // depth controls a small extra delay
    int d = (depth * 40) / 32000; // 0..40 samples
    delay_samples = 220 + d;
    mix_q15 = mix;
}

void Chorus::process(int16_t* buffer, int buffer_size) {
    for (int i = 0; i < buffer_size; ++i) {
        int32_t in = buffer[i];
        int read_idx = write_idx >= (size_t)delay_samples ? (write_idx - delay_samples) : (MAX_CHORUS_SAMPLES + write_idx - delay_samples);
        int32_t delayed = buf[read_idx];
        int32_t out = ((in * (32768 - mix_q15)) + (delayed * mix_q15)) >> 15;
        buf[write_idx] = clamp_i16(in);
        write_idx++;
        if (write_idx >= MAX_CHORUS_SAMPLES) write_idx = 0;
        buffer[i] = clamp_i16(out);
    }
}

void Chorus::reset() {
    buf.fill(0);
    write_idx = 0;
}
