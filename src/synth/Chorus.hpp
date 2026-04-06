// Chorus.hpp
#ifndef CHORUS_HPP
#define CHORUS_HPP

#include <array>
#include <cstdint>
#include "config.hpp"

class Chorus {
public:
    Chorus();
    ~Chorus();
    void set_params(int rate, int depth, int mix_q15);
    void process(int16_t* buffer, int buffer_size);
    void reset();

private:
    static const size_t MAX_CHORUS_SAMPLES = 4096;
    static const size_t CHORUS_MASK = MAX_CHORUS_SAMPLES - 1;
    std::array<int16_t, MAX_CHORUS_SAMPLES> buf;
    size_t write_idx;
    uint32_t lfo_phase;
    uint32_t lfo_phase_inc;
    int tap1_base_q8;
    int tap2_base_q8;
    int tap1_depth_q8;
    int tap2_depth_q8;
    int mix_q15;
};

#endif // CHORUS_HPP
