// Reverb.cpp
#include "Reverb.hpp"
#include <cstring>

static inline int16_t clamp_i16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

Reverb::Reverb()
    : write_idx(0), delay_samples(600), feedback_q15(16384), damp_q15(8192), mix_q15(8192), prev_del(0) {
    buf.fill(0);
}

Reverb::~Reverb() {}

void Reverb::set_params(int size_ms, int damp, int mix) {
    int samp = (size_ms * SAMPLE_RATE) / 1000;
    if (samp <= 0) samp = 200;
    if ((size_t)samp >= MAX_REVERB_SAMPLES) samp = MAX_REVERB_SAMPLES - 1;
    delay_samples = samp;
    damp_q15 = damp;
    mix_q15 = mix;
    feedback_q15 = (32768 - damp_q15) / 2;
}

void Reverb::process(int16_t* buffer, int buffer_size) {
    for (int i = 0; i < buffer_size; ++i) {
        int32_t in = buffer[i];
        int read_idx = write_idx >= (size_t)delay_samples ? (write_idx - delay_samples) : (MAX_REVERB_SAMPLES + write_idx - delay_samples);
        int32_t delayed = buf[read_idx];
        int32_t damped = ((delayed * (32768 - damp_q15)) + (prev_del * damp_q15)) >> 15;
        prev_del = damped;
        int32_t out = ((in * (32768 - mix_q15)) + (damped * mix_q15)) >> 15;
        int32_t to_store = in + ((damped * feedback_q15) >> 15);
        buf[write_idx] = clamp_i16(to_store);
        write_idx++;
        if (write_idx >= MAX_REVERB_SAMPLES) write_idx = 0;
        buffer[i] = clamp_i16(out);
    }
}

void Reverb::reset() {
    buf.fill(0);
    write_idx = 0;
    prev_del = 0;
}
