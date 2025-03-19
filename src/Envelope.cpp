#include "Envelope.hpp"
#include "fixed_point.h"
#include <cstdint>
#include <cstdio>

#define FRAC_BITS 24

// // Fixed-point multiplication
// uint32_t fp_mul(uint32_t a, uint32_t b) {
//     return (uint32_t)(((int64_t)a * b) >> FRAC_BITS);
// }
//
// // Fixed-point division
// uint32_t fp_div(uint32_t a, uint32_t b) {
//     return (uint32_t)(((int64_t)a << FRAC_BITS) / b);
// }

ADSREnvelope::ADSREnvelope()
    : a(), d(16777216), s(6710886), r(16777216), in_signal(nullptr),
      trigger(0.f), state(ENV_IDLE) {} // Default constructor

ADSREnvelope::ADSREnvelope(float a_in, float d_in, float s_in, float r_in,
                           std::array<int16_t, 1156> &in_signal, float trigger)
    : in_signal(&in_signal), trigger(trigger) {
    a = q24_from_float(a_in);
    d = q24_from_float(d_in);
    s = q24_from_float(s_in);
    r = q24_from_float(r_in);
}

// uint32_t ADSREnvelope::float_to_fixed_8_24(float in) {
//     return static_cast<uint32_t>(in * FIXED_ONE);
// }

void ADSREnvelope::out() {
    uint32_t scale = 0;

    // Note released
    if (trigger < 4.5f && state != ENV_RELEASE && state != ENV_IDLE) {
        release_start_level = current_scale;
        t = 0;
        state = ENV_RELEASE;
    }

    switch (state) {
    case ENV_ATTACK:
        scale = q24_div(t, a);
        if (t >= a) {
            state = ENV_DECAY;
            t = 0;
            scale = Q24_ONE; // Force exactly 1.0 at transition
        }
        break;

    case ENV_DECAY: {
        q8_24_t one_minus_s = q24_sub(Q24_ONE, s);
        scale = Q24_ONE - q24_mul(one_minus_s, q24_div(t, d));
        if (t >= d) {
            state = ENV_SUSTAIN;
            t = 0;
            scale = s; // Force exactly sustain level at transition
        }
        break;
    }
    case ENV_SUSTAIN:

        // printf("Env Sustain\n\r");
        scale = s;
        break;

    case ENV_RELEASE:

        // printf("Env Release\n\r");
        if (trigger > 4.5f) {
            state = ENV_ATTACK;
            t = 0;
        }
        scale = q24_mul(release_start_level, (Q24_ONE - q24_div(t, r)));
        // scale = release_start_level * (1.0f - (t / r));
        if (t >= r) {
            scale = 0.0f;
            state = ENV_IDLE;
        }
        break;

    case ENV_IDLE:
        if (trigger > 4.5f) {
            state = ENV_ATTACK;
            t = 0;
        }
        scale = 0.0f;
        break;
    }

    for (uint i = 0; i < 1156; i++) {

        current_scale = scale;
        output[i] = static_cast<int16_t>(
            (static_cast<int64_t>((*in_signal)[i]) * scale) >> 24);
        t += SAMPLE_DELTA;
    }
}

void ADSREnvelope::set_trigger(float trig) {
    trigger = trig;
}


void ADSREnvelope::set_idle() {
    state = ENV_IDLE;
}

std::array<int16_t, 1156> &ADSREnvelope::get_output() { return output; }
