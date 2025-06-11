#include "Envelope.hpp"
#include "config.hpp"
#include "fixed_point.h"
#include <cstdint>
#include <cstdio>

#define FRAC_BITS 24

ADSREnvelope::ADSREnvelope()
    : a(64), d(64), s(64), r(64), in_signal(nullptr), state(ENV_IDLE) {
} // Default constructor

ADSREnvelope::ADSREnvelope(uint8_t a_in, uint8_t d_in, uint8_t s_in,
                           uint8_t r_in,
                           std::array<int16_t, SAMPLES_PER_BUFFER> &in_signal)
    : a(a_in), d(d_in), s(s_in << 8), r(r_in), in_signal(&in_signal) {}

void ADSREnvelope::out() {

    for (uint i = 0; i < SAMPLES_PER_BUFFER; i++) {
        switch (state) {
        case ENV_ATTACK:
            current_level += a;           // Just addition!
            if (current_level >= 32768) { // 1.0 in Q1.15
                current_level = 32768;
                state = ENV_DECAY;
            }
            // printf("ENV ATTACK\n");
            break;

        case ENV_DECAY:
            current_level -= d;       // Just subtraction!
            if (current_level <= s) { // Reached sustain level
                current_level = s;
                state = s ? ENV_SUSTAIN : ENV_IDLE;
            }
            // printf("ENV DECAY\n");
            break;

        case ENV_SUSTAIN:
            current_level = s; // Hold sustain level
            // printf("ENV SUSTAIN\n");
            break;

        case ENV_RELEASE:
            if (current_level > r) {
                current_level -= r;
            } else {
                current_level = 0;
                state = ENV_IDLE;
            }
            break;

        case ENV_IDLE:
            current_level = 0;
            break;
        }

        // Apply envelope to signal
        output[i] = ((*in_signal)[i] * current_level) >> 15;
    }
}

void ADSREnvelope::update_deltas() {
    // Attack: 0 to 1.0 over attack_time_samples
    // uint32_t attack_samples =
    //     ((uint32_t)a * SAMPLE_RATE) >> 8; // Q8.8 to samples
    // if (attack_samples > 0) {
    //     attack_delta =
    //         32768 / attack_samples; // 1.0 in Q1.15 / samples = Q2.14 delta
    // }
    //
    // // Decay: 1.0 to sustain over decay_time_samples
    // uint32_t decay_samples = ((uint32_t)d * SAMPLE_RATE) >> 8;
    // if (decay_samples > 0) {
    //     decay_delta = (32768 - s) / decay_samples; // (1.0 - sustain) /
    //     samples
    // }

    attack_delta = 512;
    decay_delta = 512;
    s = 20000;
    release_delta = 512;

    // Release delta calculated when release starts (since it depends on current
    // level)
}

void ADSREnvelope::gate_on() {
    current_level = 0;
    state = ENV_ATTACK;
}

void ADSREnvelope::gate_off() { state = ENV_RELEASE; }

void ADSREnvelope::set_ADSR(uint8_t a_in, uint8_t d_in, uint8_t s_in,
                            uint8_t r_in) {
    a = a_in;
    d = d_in;
    s = s_in;
    r = r_in;
}

void ADSREnvelope::increment_ADSR(uint8_t which, int16_t delta_q15) {
    switch (which) {
    case 0: // Attack (Q8.8 format)
    {
        int32_t new_a = a + (delta_q15 >> 7); // Convert Q1.15 to Q8.8
        a = (new_a < 0) ? 0 : (uint16_t)new_a;
    } break;

    case 1: // Decay (Q8.8 format)
    {
        int32_t new_d = d + (delta_q15 >> 7); // Convert Q1.15 to Q8.8
        d = (new_d < 0) ? 0 : (uint16_t)new_d;
    } break;

    case 2: // Sustain (Q1.15 format)
    {
        int32_t new_s = s + delta_q15; // Both Q1.15, direct addition
        if (new_s < 0)
            s = 0;
        else if (new_s > 32767) // Max value for Q1.15 (0.999...)
            s = 32767;
        else
            s = (uint16_t)new_s;
    } break;

    case 3: // Release (Q8.8 format)
    {
        int32_t new_r = r + (delta_q15 >> 7); // Convert Q1.15 to Q8.8
        r = (new_r < 0) ? 0 : (uint16_t)new_r;
    } break;

    default:
        break;
    }

    // Recalculate deltas since parameters changed
    update_deltas();
}

std::array<uint16_t, 4> ADSREnvelope::get_ADSR() { return {a, d, s, r}; }

void ADSREnvelope::get_ADSR_strings(char out[4][8]) {
    const uint16_t params[4] = {a, d, s, r};

    for (int i = 0; i < 4; ++i) {
        float val =
            (i == 2) ? q1_15_to_float(params[i]) : q8_8_to_float(params[i]);
        snprintf(out[i], 8, "%.2f", val); // Format with 2 decimal digits
    }
}

void ADSREnvelope::set_idle() { state = ENV_IDLE; }

std::array<int16_t, SAMPLES_PER_BUFFER> &ADSREnvelope::get_output() {
    return output;
}
