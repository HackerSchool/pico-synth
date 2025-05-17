#include "Oscillator.hpp"
#include "Wavetable.hpp"
#include "config.hpp"

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

WaveType Oscillator::get_wave_type(){
    return wave_type_;
}

void Oscillator::out() {
    const uint32_t pos_mask = (WAVE_TABLE_LEN << 16) - 1;

    for (int i = 0; i < SAMPLES_PER_BUFFER; i++) {

        // Extract the integer part of the position (top 16 bits)
        uint32_t pos_int = pos >> 16;
        output[i] = (*wavetable_)[pos_int];

        // Increment position and wrap around using bitwise operations
        pos = (pos + step) & pos_mask;
    }
}

std::array<int16_t, SAMPLES_PER_BUFFER> &Oscillator::get_output() {
    return output;
}

void Oscillator::set_freq(float new_freq) {
    // Convert to fixed-point (16.16 format)
    step = static_cast<uint32_t>((WAVE_TABLE_LEN * new_freq / 44100.0f) *
                                 65536.0f);
}
