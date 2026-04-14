#include "Oscillator.hpp"
#include "MidiHandler.hpp"
#include "Wavetable.hpp"
#include "config.hpp"
#include "hardware/interp.h"
#include "tusb.h"
#include <cstdint>

const int wave_shift = WAVE_SHIFT;
const int wave_len = WAVE_LEN;
const int wave_max = WAVE_MAX;

constexpr float MAGIC_NUMBER = 1.5625f; // your overclock factor

// compile time LUT for the dco_steps
const std::array<uint32_t, 128> note_table{
    []() {
        std::array<uint32_t, 128> table{};
        for (int i = 0; i < 128; i++) {
            // Calculate step as fixed point 16.16
            float step_f = (WAVE_TABLE_LEN * midi_frequencies[i] *
                            MAGIC_NUMBER * 65536.0f) /
                           SAMPLE_RATE;
            table[i] = static_cast<uint32_t>(step_f);
        }
        return table;
    }()};

Oscillator::Oscillator()
    : freq(440.f), wavetable_(&sine_wave_table), step(0), pos(0) {
} // Default constructor

Oscillator::Oscillator(WaveType wave_type, float freq) : freq(freq) {
    set_wavetable(wave_type);
    set_freq(freq);
}

void Oscillator::set_wavetable(WaveType wave_type) {
    // Choose the wavetable based on wave type
    switch (wave_type) {
    case Sine:
        wavetable_ = &sine_wave_table;
        break;
    case Square:
        wavetable_ = &square_wave_table;
        break;
    case Triangle:
        wavetable_ = &triangle_wave_table;
        break;
    case Sawtooth:
        wavetable_ = &sawtooth_wave_table;
        break;
    case Sinc:
        wavetable_ = &sinc_table;
        break;
    default:
        wavetable_ = &sine_wave_table;
        break;
    }
    wave_type_ = wave_type;
}

WaveType Oscillator::get_wave_type() { return wave_type_; }

// void Oscillator::out() {
//     const uint32_t pos_mask = (WAVE_TABLE_LEN << 16) - 1;
//     for (int i = 0; i < SAMPLES_PER_BUFFER; i++) {
//         // Extract integer and fractional parts
//         uint16_t pos_int = pos >> 16;
//         uint16_t frac = pos & 0xFFFF; // Bottom 16 bits = fractional part
//
//         // Get current and next samples
//         int16_t sample0 = (*wavetable_)[pos_int];
//         int16_t sample1 = (*wavetable_)[(pos_int + 1) & (WAVE_TABLE_LEN - 1)];
//
//         // Linear interpolation using shifts instead of division
//         // result = sample0 + (sample1 - sample0) * frac / 65536
//         // But we use shifts: frac is already 0-65535, so just shift right
//         int32_t diff = sample1 - sample0;
//         int32_t interpolated = sample0 + ((diff * frac) >> 16);
//
//         output[i] = (int16_t)interpolated;
//
//         pos = (pos + step) & pos_mask;
//     }
// }

void Oscillator::out_interp(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer,
                            uint8_t fm_depth_) {
    // FM depth in Q16.16 format - starting at 0.5 (32768 in Q16.16)
    uint32_t fm_offset = 0;

    // Set up carrier oscillator (interp0)
    interp0->base[0] = dco_step; // TODO: fm ration here
    interp0->base[2] = (uintptr_t)wavetable_->data();
    interp0->accum[0] = dco_pos;

    // Generate samples
    for (uint i = 0; i < SAMPLES_PER_BUFFER; ++i) {
        // Get modulator sample (signed 16-bit)
        // int16_t mod_sample = *(int16_t *)interp1->pop[2];

        // Apply FM: mod_sample * fm_depth -> frequency offset
        // Convert to Q16.16: (mod_sample * fm_depth_q16_16) >> 16
        if (fm_depth_) {
            fm_offset = (buffer[i] * (fm_depth_ << 3));
        }

        // Update carrier frequency with modulation
        interp0->base[0] = dco_step + fm_offset;

        // Get carrier output
        buffer[i] = *(int16_t *)interp0->pop[2];
    }

    // Update oscillator positions
    dco_pos = interp0->accum[0] & (wave_max - 1);
}

void Oscillator::set_dco_step(uint8_t note, uint8_t ratio) {
    dco_step = note_table[note] * ratio;
}


void Oscillator::reset_dco_pos() {
    dco_pos = 0;
}

// std::array<int16_t, SAMPLES_PER_BUFFER> &Oscillator::get_output() {
//     return output;
// }

void Oscillator::set_freq(float new_freq) {
    // Convert to fixed-point (16.16 format)
    float magic_number = 1.5625; // its because we overclock from 96 to 150MHz
    step = static_cast<uint32_t>(WAVE_TABLE_LEN * new_freq * magic_number *
                                 65536.0f / 44100.0f);
    // float step_print =
    //     WAVE_TABLE_LEN * new_freq * magic_number * 65536.0f / 44100.0f;
    //
    // printf("Step: %f\n", step_print);
    // pos = 0;
}
