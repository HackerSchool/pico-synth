#ifndef OSCILLATOR_HPP
#define OSCILLATOR_HPP

#include "Wavetable.hpp"
#include "fixed_point.h"
#include <array>
#include <cstdint>

class Oscillator {
  public:
    Oscillator();
    Oscillator(WaveType wave_type, float freq);

    void out();
    std::array<int16_t, 1156> &get_output();
    void set_freq(float new_freq);


  private:
    const std::array<int16_t, WAVE_TABLE_LEN> *wavetable_;
    std::array<int16_t, 1156> output = {};
    q16_16_t pos = 0;           // Fixed-point position (16.16 format)
    q16_16_t step;          // Fixed-point step size (16.16 format)
    float freq;
};

#endif // !OSCILLATOR_HPP
