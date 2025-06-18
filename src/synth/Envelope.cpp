#include "Envelope.hpp"
#include "Wavetable.hpp"
#include "config.hpp"
#include "fixed_point.h"
#include <cstdint>
#include <cstdio>

#define FRAC_BITS 24

ADSREnvelope::ADSREnvelope()
    : a(64), d(64), s(64 << 8), r(64), state(ENV_IDLE) {} // Default constructor

ADSREnvelope::ADSREnvelope(uint8_t a_in, uint8_t d_in, uint8_t s_in,
                           uint8_t r_in)
    : a(a_in), d(d_in), s(s_in << 8), r(r_in) {}

void ADSREnvelope::out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer) {
    auto &lv = current_level; // Shorter reference for current_level

    for (uint i = 0; i < SAMPLES_PER_BUFFER; i++) {
        switch (state) {
        case ENV_ATTACK:
            lv += a;                     // 32-bit addition
            if (lv >= (32768UL << 16)) { // 1.0 in Q16.16
                lv = (32768UL << 16);
                state = ENV_DECAY;
            }
            break;
        case ENV_DECAY:
            if (lv > d) {
                lv -= d; // 32-bit subtraction
                if (lv <=
                    (static_cast<uint32_t>(s << 16))) { // Cast to uint32_t
                    lv = (static_cast<uint32_t>(s << 16));
                    state = s ? ENV_SUSTAIN : ENV_IDLE;
                }
            } else {
                lv = (static_cast<uint32_t>(s << 16));
                state = s ? ENV_SUSTAIN : ENV_IDLE;
            }
            break;
        case ENV_SUSTAIN:
            lv = static_cast<uint32_t>(s << 16); // Cast to uint32_t
            break;
        case ENV_RELEASE:
            if (lv > r) {
                lv -= r;
            } else {
                lv = 0;
                state = ENV_IDLE;
            }
            break;
        case ENV_IDLE:
            lv = 0;
            break;
        }

        // Use upper 16 bits for multiplication (Q16.16 -> Q1.15)
        uint16_t level_q15 = lv >> 16;
        // output[i] = (in[i] * level_q15) >> 15;
        buffer[i] = (buffer[i] * level_q15) >> 15;
    }
}

void ADSREnvelope::gate_on() {
    current_level = 0;
    state = ENV_ATTACK;
}

void ADSREnvelope::gate_off() { state = ENV_RELEASE; }

void ADSREnvelope::set_ADSR(uint8_t a_in, uint8_t d_in, uint8_t s_in,
                            uint8_t r_in) {
    a = adsr_curve_table[a_in]; // Now 1-403 range instead of 0-127
    d = adsr_curve_table[d_in];
    r = adsr_curve_table[r_in];
    s = s_in << 8; // Sustain is still linear level (0-32768)
}

std::array<uint32_t, 4> ADSREnvelope::get_ADSR() { return {a, d, s, r}; }

// void ADSREnvelope::get_ADSR_strings(char out[4][8]) {
//     const uint32_t params[4] = {a, d, s, r};
//
//     for (int i = 0; i < 4; ++i) {
//         float val =
//             (i == 2) ? q1_15_to_float(params[i]) : q8_8_to_float(params[i]);
//         snprintf(out[i], 8, "%.2f", val); // Format with 2 decimal digits
//     }
// }

void ADSREnvelope::set_idle() { state = ENV_IDLE; }

// std::array<int16_t, SAMPLES_PER_BUFFER> &ADSREnvelope::get_output() {
//     return output;
// }
