#ifndef OSCILLATOR_HPP
#define OSCILLATOR_HPP

#include "Wavetable.hpp"
#include "config.hpp"
#include "fixed_point.h"
#include <array>
#include <cstdint>

class Oscillator {
  public:
    Oscillator();
    Oscillator(WaveType wave_type, float freq);

    void out();
    std::array<int16_t, SAMPLES_PER_BUFFER> &get_output();
    void set_freq(float new_freq);
    void set_wavetable(WaveType wave_table);
    WaveType get_wave_type();


  private:
    WaveType wave_type_;
    const std::array<int16_t, WAVE_TABLE_LEN> *wavetable_;
    std::array<int16_t, SAMPLES_PER_BUFFER> output = {};
    q16_16_t pos = 0;           // Fixed-point position (16.16 format)
    q16_16_t step;          // Fixed-point step size (16.16 format)
    float freq;
};

#endif // !OSCILLATOR_HPP
