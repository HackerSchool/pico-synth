#include "Oscillator.hpp"
#include "Wavetable.hpp"
#include "config.hpp"
#include "tusb.h"

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

void Oscillator::out() {
    const uint32_t pos_mask = (WAVE_TABLE_LEN << 16) - 1;
    for (int i = 0; i < SAMPLES_PER_BUFFER; i++) {
        // Extract integer and fractional parts
        uint16_t pos_int = pos >> 16;
        uint16_t frac = pos & 0xFFFF;  // Bottom 16 bits = fractional part
        
        // Get current and next samples
        int16_t sample0 = (*wavetable_)[pos_int];
        int16_t sample1 = (*wavetable_)[(pos_int + 1) & (WAVE_TABLE_LEN - 1)];
        
        // Linear interpolation using shifts instead of division
        // result = sample0 + (sample1 - sample0) * frac / 65536
        // But we use shifts: frac is already 0-65535, so just shift right
        int32_t diff = sample1 - sample0;
        int32_t interpolated = sample0 + ((diff * frac) >> 16);
        
        output[i] = (int16_t)interpolated;
        
        pos = (pos + step) & pos_mask;
    }
}

std::array<int16_t, SAMPLES_PER_BUFFER> &Oscillator::get_output() {
    return output;
}

void Oscillator::set_freq(float new_freq) {
    // Convert to fixed-point (16.16 format)
    float magic_number = 1.5625; // its because we overclock from 96 to 150MHz
        step = static_cast<uint32_t>(WAVE_TABLE_LEN * new_freq * magic_number *
                                     65536.0f / 44100.0f);
    float step_print = WAVE_TABLE_LEN * new_freq * magic_number *
                                     65536.0f / 44100.0f;

    printf("Step: %f\n", step_print);
    // pos = 0;
}
