#include "Oscillator.hpp"
#include "Wavetable.hpp"

Oscillator::Oscillator()
    : freq(440.f), wavetable_(&sine_wave_table), step(0), pos(0) {
} // Default constructor

Oscillator::Oscillator(WaveType wave_type, float freq) : freq(freq) {
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

    set_freq(freq);
}

void Oscillator::out() {
    const uint32_t pos_mask = (WAVE_TABLE_LEN << 16) - 1;

    for (int i = 0; i < 1156; i++) {

        // Extract the integer part of the position (top 16 bits)
        uint32_t pos_int = pos >> 16;
        output[i] = (*wavetable_)[pos_int];

        // Increment position and wrap around using bitwise operations
        pos = (pos + step) & pos_mask;
    }
}

std::array<int16_t, 1156> &Oscillator::get_output() { return output; }

void Oscillator::set_freq(float new_freq) {
    // Convert to fixed-point (16.16 format)
    step = static_cast<uint32_t>((WAVE_TABLE_LEN * new_freq / 44100.0f) *
                                 65536.0f);
    pos = 0;
}
