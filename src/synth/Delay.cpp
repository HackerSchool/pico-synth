#include "Delay.hpp"
#include <algorithm>

// Helper function for integer max/min (avoiding std::max with different types)
inline int int_max(int a, int b) { return (a > b) ? a : b; }
inline int int_min(int a, int b) { return (a < b) ? a : b; }
inline int16_t int16_max(int16_t a, int16_t b) { return (a > b) ? a : b; }
inline int16_t int16_min(int16_t a, int16_t b) { return (a < b) ? a : b; }

Delay::Delay() : write_index(0), delay_samples(11025), feedback(0), mix(0) {
    // Initialize delay buffer to zero
    delay_buffer.fill(0);
    
    // Set default values: 250ms delay, ~30% feedback, ~30% mix
    set_delay_ms(250);
    set_feedback(10000);   // ~0.3 in Q1.15 format (9830/32767 ≈ 0.3)
    set_mix(10000);        // ~0.3 in Q1.15 format
}

void Delay::set_delay_samples(int samples) {
    // Clamp to valid range
    delay_samples = int_max(1, int_min(samples, MAX_DELAY_SAMPLES - 1));
}

void Delay::set_delay_ms(int ms) {
    // Convert milliseconds to samples using integer arithmetic
    // samples = ms * SAMPLE_RATE / 1000
    int samples = (ms * SAMPLE_RATE) / 1000;
    set_delay_samples(samples);
}

void Delay::set_feedback(int16_t fb_q1_15) {
    // Clamp feedback to safe range (0 to ~0.95 in Q1.15)
    // 31000 in Q1.15 ≈ 0.946
    // Lmao. no i dont want safety
    feedback = int16_max(0, int16_min(fb_q1_15, 32767));
}

void Delay::set_mix(int16_t mix_q1_15) {
    // Clamp mix to valid range (0 to 32767 in Q1.15)
    mix = int16_max(0, int16_min(mix_q1_15, 32767));
}

void Delay::process(int16_t* buffer, int buffer_size) {
    for (int i = 0; i < buffer_size; i++) {
        // Calculate read index for delayed sample
        int read_index = (write_index - delay_samples + MAX_DELAY_SAMPLES) % MAX_DELAY_SAMPLES;
        
        // Get delayed sample
        int16_t delayed_sample = delay_buffer[read_index];
        
        // Calculate dry/wet mix using fixed-point arithmetic
        // dry_signal * (1.0 - mix) + wet_signal * mix
        int32_t dry_signal = buffer[i];
        int32_t wet_signal = delayed_sample;
        
        // Calculate (1.0 - mix) in Q1.15 format
        int16_t one_minus_mix = 32767 - mix;
        
        // Mix calculation: dry * (1-mix) + wet * mix
        int32_t mixed_output = ((dry_signal * one_minus_mix) >> 15) + 
                              ((wet_signal * mix) >> 15);
        
        // Clamp output to prevent overflow
        if (mixed_output > INT16_MAX) mixed_output = INT16_MAX;
        if (mixed_output < INT16_MIN) mixed_output = INT16_MIN;
        
        // Calculate feedback: delayed_sample * feedback
        int32_t feedback_signal = (delayed_sample * feedback) >> 15;
        
        // Input to delay buffer: current input + feedback
        int32_t delay_input = dry_signal + feedback_signal;
        
        // Clamp delay input to prevent overflow
        if (delay_input > INT16_MAX) delay_input = INT16_MAX;
        if (delay_input < INT16_MIN) delay_input = INT16_MIN;
        
        // Write to delay buffer
        delay_buffer[write_index] = static_cast<int16_t>(delay_input);
        
        // Update output buffer
        buffer[i] = static_cast<int16_t>(mixed_output);
        
        // Advance write index
        write_index = (write_index + 1) % MAX_DELAY_SAMPLES;
    }
}

void Delay::reset() {
    delay_buffer.fill(0);
    write_index = 0;
}