#ifndef OSCILLATOR_HPP
#define OSCILLATOR_HPP

#include "Wavetable.hpp"
#include <array>
#include <cstdint>

class Oscillator {
public:
    Oscillator(WaveType wave_type, float freq);

    void out();
    std::array<int16_t, 1156> &get_output();
    void set_freq(float new_freq);

private:
    const std::array<int16_t, WAVE_TABLE_LEN> *wavetable_;
    std::array<int16_t, 1156> output = {};
    float freq;
    float step;
    float pos = 0.0f;
};

#endif // !OSCILLATOR_HPP
