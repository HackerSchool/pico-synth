// Reverb.hpp
#ifndef REVERB_HPP
#define REVERB_HPP

#include <array>
#include <cstdint>
#include "config.hpp"

class Reverb {
public:
    Reverb();
    ~Reverb();
    void set_params(int size_ms, int damp_q15, int mix_q15);
    void process(int16_t* buffer, int buffer_size);
    void reset();

private:
    static const size_t MAX_REVERB_SAMPLES = 8192; // ~0.18s at 44.1k
    std::array<int16_t, MAX_REVERB_SAMPLES> buf;
    size_t write_idx;
    int delay_samples;
    int feedback_q15;
    int damp_q15;
    int mix_q15;
    int32_t prev_del;
};

#endif // REVERB_HPP
