/*
 * Delay effect implementation:
 * - Standard mono feedback delay line with in-place dry/wet mixing.
 * - Delay-time changes are slewed one sample at a time to reduce zipper noise.
 * - The feedback loop uses a simple one-pole low-pass to darken repeats.
 * - A cubic soft clipper in the write path keeps high-feedback settings musical.
 */

#include "Delay.hpp"

inline int int_max(int a, int b) { return (a > b) ? a : b; }
inline int int_min(int a, int b) { return (a < b) ? a : b; }
inline int16_t int16_max(int16_t a, int16_t b) { return (a > b) ? a : b; }
inline int16_t int16_min(int16_t a, int16_t b) { return (a < b) ? a : b; }

static inline int16_t clamp_i16(int32_t v) {
    if (v > INT16_MAX) return INT16_MAX;
    if (v < INT16_MIN) return INT16_MIN;
    return static_cast<int16_t>(v);
}

static inline int32_t cubic_soft_clip_i16(int32_t x) {
    if (x >= 32767) return 32767;
    if (x <= -32768) return -32768;

    const int32_t x_q15 = x;
    const int32_t x2 = static_cast<int32_t>((static_cast<int64_t>(x_q15) * x_q15) >> 15);
    const int32_t x3 = static_cast<int32_t>((static_cast<int64_t>(x2) * x_q15) >> 15);
    return static_cast<int32_t>((static_cast<int64_t>(x_q15 - (x3 / 3)) * 3) >> 1);
}

Delay::Delay()
    : write_index(0),
      current_delay_samples(11025),
      target_delay_samples(11025),
      feedback(0),
      mix(0),
      feedback_lp_state(0),
      feedback_damp_q15(12288) {
    delay_buffer.fill(0);
    set_delay_ms(250);
    set_feedback(10000);
    set_mix(10000);
}

void Delay::set_delay_samples(int samples) {
    target_delay_samples = int_max(1, int_min(samples, MAX_DELAY_SAMPLES - 1));
}

void Delay::set_delay_ms(int ms) {
    const int samples = (ms * SAMPLE_RATE) / 1000;
    set_delay_samples(samples);
}

void Delay::set_feedback(int16_t fb_q1_15) {
    feedback = int16_max(0, int16_min(fb_q1_15, 32767));
    feedback_damp_q15 = static_cast<int16_t>(16384 - (feedback >> 2));
}

void Delay::set_mix(int16_t mix_q1_15) {
    mix = int16_max(0, int16_min(mix_q1_15, 32767));
}

void Delay::process(int16_t* buffer, int buffer_size) {
    for (int i = 0; i < buffer_size; ++i) {
        if (current_delay_samples < target_delay_samples) {
            ++current_delay_samples;
        } else if (current_delay_samples > target_delay_samples) {
            --current_delay_samples;
        }

        int read_index = write_index - current_delay_samples;
        if (read_index < 0) {
            read_index += MAX_DELAY_SAMPLES;
        }

        const int16_t delayed_sample = delay_buffer[read_index];
        const int32_t dry_signal = buffer[i];
        const int32_t wet_signal = delayed_sample;
        const int16_t one_minus_mix = 32767 - mix;

        const int32_t mixed_output =
            ((dry_signal * one_minus_mix) >> 15) + ((wet_signal * mix) >> 15);

        // Damping in the feedback loop keeps repeats from getting brittle,
        // and soft clipping avoids abrupt digital runaway at high feedback.
        feedback_lp_state += static_cast<int32_t>(
            (static_cast<int64_t>(delayed_sample - feedback_lp_state) * feedback_damp_q15) >> 15);
        const int32_t feedback_signal = static_cast<int32_t>(
            (static_cast<int64_t>(feedback_lp_state) * feedback) >> 15);

        int32_t delay_input = dry_signal + feedback_signal;
        delay_input = cubic_soft_clip_i16(delay_input);

        delay_buffer[write_index] = clamp_i16(delay_input);
        buffer[i] = clamp_i16(mixed_output);

        ++write_index;
        if (write_index >= MAX_DELAY_SAMPLES) {
            write_index = 0;
        }
    }
}

void Delay::reset() {
    delay_buffer.fill(0);
    write_index = 0;
    current_delay_samples = target_delay_samples;
    feedback_lp_state = 0;
}
