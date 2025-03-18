#include "Envelope.hpp"
#include <cstdint>
#include <cstdio>


#define FRAC_BITS 24

// Fixed-point multiplication
uint32_t fp_mul(uint32_t a, uint32_t b) {
    return (uint32_t)(((int64_t)a * b) >> FRAC_BITS);
}

// Fixed-point division
uint32_t fp_div(uint32_t a, uint32_t b) {
    return (uint32_t)(((int64_t)a << FRAC_BITS) / b);
}

ADSREnvelope::ADSREnvelope()
    : a(8388608), d(1677722), s(6710886), r(16777216), in_signal(nullptr),
      trigger(0.f), state(ENV_IDLE) {} // Default constructor

ADSREnvelope::ADSREnvelope(float a_in, float d_in, float s_in, float r_in,
                           std::array<int16_t, 1156> &in_signal, float trigger)
    : in_signal(&in_signal), trigger(trigger) {
    a = float_to_fixed_8_24(a_in);
    d = float_to_fixed_8_24(d_in);
    s = float_to_fixed_8_24(s_in);
    r = float_to_fixed_8_24(r_in);
}

uint32_t ADSREnvelope::float_to_fixed_8_24(float in) {
    return static_cast<uint32_t>(in * FIXED_ONE);
}

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
        // printf("Env Attack\n\r");

        // scale = (int32_t)(((int64_t)t << 24) / a);
        scale = fp_div(t, a);
        // scale = t / a;
        if (t >= a) {
            state = ENV_DECAY;
            t = 0;
        }
        break;

    case ENV_DECAY: {

        // printf("Env Decay\n\r");
        uint32_t one_minus_s = FIXED_ONE - s;

        scale = FIXED_ONE - fp_mul(one_minus_s, fp_div(d, t));
        // scale = FIXED_ONE - ((one_minus_s * t) / d);
        // scale = 1.0f - ((1.0f - s) * (t / d));
        if (t >= d) {
            state = ENV_SUSTAIN;
            t = 0;
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
        scale = fp_mul(release_start_level, (FIXED_ONE - fp_div(t, r)));
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

void ADSREnvelope::set_trigger(float trig) { trigger = trig; }

std::array<int16_t, 1156> &ADSREnvelope::get_output() { return output; }
