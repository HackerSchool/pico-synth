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

    void out_interp(uint8_t fm_depth_);
    void set_dco_step(uint8_t note);

  private:
    WaveType wave_type_;
    int32_t fm_depth =
        256 << 16; // ok, lets use the 32bit envelope trick to get slower ramps
    const std::array<int16_t, WAVE_TABLE_LEN> *wavetable_;
    std::array<int16_t, SAMPLES_PER_BUFFER> output = {};
    q16_16_t pos = 0; // Fixed-point position (16.16 format)
    q16_16_t step;    // Fixed-point step size (16.16 format)
    float freq;

    uint32_t dco_step_base;
    uint32_t dco_step;
    uint32_t dco_pos;
    uint32_t dco_mod_pos;
};

#endif // !OSCILLATOR_HPP
