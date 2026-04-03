// Distortion.cpp
#include "Distortion.hpp"
#include <cstring>

static inline int16_t clamp_i16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

Distortion::Distortion()
    : drive_q15(0), thresh(16000), mix_q15(16384) {}

void Distortion::set_params(int drive, int th, int mix) {
    drive_q15 = drive;
    thresh = (int16_t)(th > 0 ? th : 16000);
    mix_q15 = mix;
}

void Distortion::process(int16_t* buffer, int buffer_size) {
    int32_t mult = 16384 + drive_q15; // Q14 multiplier
    for (int i = 0; i < buffer_size; ++i) {
        int32_t dry = buffer[i];
        int32_t driven = (dry * mult) >> 14;
        int32_t clipped = driven;
        if (driven > thresh) clipped = thresh;
        else if (driven < -thresh) clipped = -thresh;
        int32_t out = ((dry * (32768 - mix_q15)) + (clipped * mix_q15)) >> 15;
        buffer[i] = clamp_i16(out);
    }
}

void Distortion::reset() {
    drive_q15 = 0;
    thresh = 16000;
    mix_q15 = 16384;
}
