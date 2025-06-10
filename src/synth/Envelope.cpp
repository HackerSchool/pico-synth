#include "Envelope.hpp"
#include "config.hpp"
#include "fixed_point.h"
#include <cstdint>
#include <cstdio>

#define FRAC_BITS 24

ADSREnvelope::ADSREnvelope()
    : a(1.0), d(1.0), s(1.0), r(1.0), in_signal(nullptr), trigger(0.f),
      state(ENV_IDLE) {} // Default constructor

ADSREnvelope::ADSREnvelope(float a_in, float d_in, float s_in, float r_in,
                           std::array<int16_t, SAMPLES_PER_BUFFER> &in_signal,
                           float trigger)
    : in_signal(&in_signal), trigger(trigger) {
    a = float_to_q8_8(a_in);
    d = float_to_q8_8(d_in);
    s = float_to_q1_15(s_in);
    r = float_to_q8_8(r_in);

    // Recalculate deltas
    update_deltas();
}

void ADSREnvelope::out() {
    // Note released - calculate release delta when starting release
    // if (trigger < 4.5f && state != ENV_RELEASE && state != ENV_IDLE) {
    //     // In the release trigger section:
    //     // uint32_t release_samples = ((uint32_t)r * SAMPLE_RATE) >> 8;
    //     // if (release_samples > 0) {
    //     //     release_delta =
    //     //         current_level / release_samples; // This stays the same
    //     // } else {
    //     //     release_delta = current_level;
    //     // }
    //
    //     state = ENV_RELEASE;
    // }

    // int16_t current_delta = 0;
    switch (state) {
    case ENV_ATTACK:
        current_level += attack_delta; // Just addition!
        if (current_level >= 32768) {  // 1.0 in Q1.15
            current_level = 32768;
            state = ENV_DECAY;
        }
        // printf("ENV ATTACK\n");
        break;

    case ENV_DECAY:
        current_level -= decay_delta; // Just subtraction!
        if (current_level <= s) {     // Reached sustain level
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
        // if (trigger > 4.5f) { // Retrigger during release
        //     state = ENV_ATTACK;
        //     // Keep current_level as-is for smooth transition
        //     break;
        // }
        if (current_level > release_delta) {
            current_level -= release_delta;
        } else {
            current_level = 0;
            state = ENV_IDLE;
        }
        // printf("ENV RELEASE\n");
        break;

    case ENV_IDLE:
        if (trigger > 4.5f) {
            current_level = 0; // Start from 0
            state = ENV_ATTACK;
        }
        // printf("ENV IDLE\n");
        current_level = 0;
        break;
    }

    for (uint i = 0; i < SAMPLES_PER_BUFFER; i++) {
        // Convert Q1.15 envelope to Q2.14 for multiplication
        // Check for overflow using the sign bit after addition
        // Check if addition would overflow by looking at signs
        // int32_t temp = current_level + current_delta;
        // current_level = temp & ~(temp >> 31); // Zero if negative
        // if (current_level > 32767)
        //     current_level = 32767;

        // int16_t scale_q2_14 = current_level >> 1; // Q1.15 -> Q2.14

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

    attack_delta = 1048;
    decay_delta = 1048;
    s = 20000;
    release_delta = 1048;

    // Release delta calculated when release starts (since it depends on current
    // level)
}

void ADSREnvelope::set_trigger(float trig) { trigger = trig; }

void ADSREnvelope::gate_on()
{
	 state = ENV_ATTACK;
}

void ADSREnvelope::gate_off()
{
	state = ENV_RELEASE;
}

void ADSREnvelope::set_ADSR(float a_in, float d_in, float s_in, float r_in) {
    a = float_to_q8_8(a_in);
    d = float_to_q8_8(d_in);
    s = float_to_q1_15(s_in);
    r = float_to_q8_8(r_in);

    // Recalculate deltas
    update_deltas();
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
