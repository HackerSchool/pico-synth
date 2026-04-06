#ifndef DELAY_HPP
#define DELAY_HPP

#include <array>
#include <cstdint>
#include "config.hpp"

class Delay {
private:
    static const int MAX_DELAY_SAMPLES = 22050;  // 0.5 seconds at 44.1kHz
    std::array<int16_t, MAX_DELAY_SAMPLES> delay_buffer;
    int write_index;
    int current_delay_samples;
    int target_delay_samples;
    int16_t feedback;     // Q1.15 format
    int16_t mix;          // Q1.15 format
    int32_t feedback_lp_state;
    int16_t feedback_damp_q15;

public:
    Delay();

    void set_delay_samples(int samples);
    void set_delay_ms(int ms);
    void set_feedback(int16_t fb_q1_15);
    void set_mix(int16_t mix_q1_15);
    void process(int16_t* buffer, int buffer_size);
    void reset();

    int get_delay_samples() const { return target_delay_samples; }
};

#endif // DELAY_HPP
