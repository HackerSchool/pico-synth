#include "Envelope.hpp"
#include "config.hpp"
#include "fixed_point.h"
#include <cstdint>
#include <cstdio>

#define FRAC_BITS 24

ADSREnvelope::ADSREnvelope()
    : a(), d(16777216), s(6710886), r(16777216), in_signal(nullptr),
      trigger(0.f), state(ENV_IDLE) {} // Default constructor

ADSREnvelope::ADSREnvelope(float a_in, float d_in, float s_in, float r_in,
                           std::array<int16_t, SAMPLES_PER_BUFFER> &in_signal,
                           float trigger)
    : in_signal(&in_signal), trigger(trigger) {
    a = q24_from_float(a_in);
    d = q24_from_float(d_in);
    s = q24_from_float(s_in);
    r = q24_from_float(r_in);
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

    for (uint i = 0; i < SAMPLES_PER_BUFFER; i++) {

        current_scale = scale;
        int16_t scale_q2_14 = (int16_t)(scale >> 10);
        output[i] = static_cast<int16_t>(
            // (static_cast<int64_t>((*in_signal)[i]) * scale) >> 24);
            (static_cast<int32_t>((*in_signal)[i]) * scale_q2_14) >> 14);
        t += SAMPLE_DELTA;
    }
}

void ADSREnvelope::set_trigger(float trig) { trigger = trig; }

void ADSREnvelope::set_ADSR(float a_in, float d_in, float s_in, float r_in) {
    a = q24_from_float(a_in);
    d = q24_from_float(d_in);
    s = q24_from_float(s_in);
    r = q24_from_float(r_in);
}

void ADSREnvelope::increment_ADSR(uint8_t which, int32_t delta_q24) {
    uint32_t zero = 0;
    switch (which) {
    case 0: // Attack
        a = std::max(zero, static_cast<uint32_t>(a + delta_q24));
        break;
    case 1: // Decay
        d = std::max(zero, static_cast<uint32_t>(d + delta_q24));
        break;
    case 2: // Sustain (clamp to [0, 1.0] in Q8.24)
        s += delta_q24;
        if (s < 0)
            s = 0;
        if (s > (1 << 24))
            s = (1 << 24);
        break;
    case 3: // Release
        r = std::max(zero, static_cast<uint32_t>(r + delta_q24));
        break;
    default:
        break;
    }
}

std::array<int32_t, 4> ADSREnvelope::get_ADSR() { return {a, d, s, r}; }

void ADSREnvelope::get_ADSR_strings(char out[4][8]) {
    const int32_t params[4] = {a, d, s, r};

    for (int i = 0; i < 4; ++i) {
        float val = q24_to_float(params[i]);
        snprintf(out[i], 8, "%.2f", val); // Format with 2 decimal digits
    }
}

void ADSREnvelope::set_idle() { state = ENV_IDLE; }

std::array<int16_t, SAMPLES_PER_BUFFER> &ADSREnvelope::get_output() {
    return output;
}
