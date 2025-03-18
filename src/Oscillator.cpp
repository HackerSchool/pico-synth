#include "Oscillator.hpp"

Oscillator::Oscillator() : freq(440.f), wavetable_(&sine_wave_table), step(0.f){} // Default constructor

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
    default:
        wavetable_ = &sine_wave_table;
        break;
    }

    step = WAVE_TABLE_LEN * (freq / 44100.0f);
}

void Oscillator::out() {
    for (int i = 0; i < 1156; i++) {
        output[i] = wavetable_->at(static_cast<int>(pos));
        pos += step;
        if (pos >= WAVE_TABLE_LEN) {
            pos -= WAVE_TABLE_LEN;
        }
    }
}

std::array<int16_t, 1156> &Oscillator::get_output() {
    return output;
}

void Oscillator::set_freq(float new_freq) {
    freq = new_freq;
    step = WAVE_TABLE_LEN * (freq / 44100.0f);
}
