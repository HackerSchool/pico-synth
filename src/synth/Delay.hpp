#ifndef DELAY_HPP
#define DELAY_HPP

#include <cstdint>
#include <array>
#include "config.hpp"  // For SAMPLE_RATE

class Delay {
private:
    static const int MAX_DELAY_SAMPLES = 22050;  // 0.5 seconds at 44.1kHz
    std::array<int16_t, MAX_DELAY_SAMPLES> delay_buffer;
    int write_index;
    int delay_samples;
    int16_t feedback;     // Q1.15 format (0.0 to 0.999)
    int16_t mix;          // Q1.15 format (0.0 to 1.0)

public:
    Delay();
    
    // Set delay time in samples
    void set_delay_samples(int samples);
    
    // Set delay time in milliseconds (uses global SAMPLE_RATE)
    void set_delay_ms(int ms);
    
    // Set feedback amount (0-31000 range, where 31000 ≈ 0.95 in Q1.15)
    void set_feedback(int16_t fb_q1_15);
    
    // Set dry/wet mix (0-32767 range, where 0 = dry only, 32767 = wet only)
    void set_mix(int16_t mix_q1_15);
    
    // Process audio buffer in-place
    void process(int16_t* buffer, int buffer_size);
    
    // Reset delay buffer
    void reset();
    
    // Get current delay time in samples
    int get_delay_samples() const { return delay_samples; }
};

#endif // DELAY_HPP