/*
 * Distortion effect implementation:
 * - Classic pre-gain into soft-clipping waveshaper design.
 * - Uses a cubic soft clipper as a lightweight approximation to analog saturation.
 * - Includes a simple high-pass/DC blocker before shaping to tighten the low end.
 * - A one-pole low-pass after clipping smooths the top end and controls fizz.
 */

#include "Distortion.hpp"

static inline int16_t clamp_i16(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

static inline int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int32_t cubic_soft_clip_q15(int32_t x_q15) {
    if (x_q15 >= 32768) return 21845;
    if (x_q15 <= -32768) return -21845;

    // Use 64-bit intermediate to avoid overflow and precision loss
    const int64_t x_q15_64 = static_cast<int64_t>(x_q15);
    const int64_t x2_q30 = (x_q15_64 * x_q15_64) >> 15;  // Keep extra precision in Q30
    const int64_t x3_q45 = (x2_q30 * x_q15_64) >> 15;     // x3 in Q45
    const int32_t x3_q15 = static_cast<int32_t>(x3_q45 >> 30);  // Convert back to Q15
    return x_q15 - (x3_q15 / 3);
}

Distortion::Distortion()
    : input_gain_q12(4096),
      clip_threshold(16000),
      clip_threshold_inv_q15(0),
      makeup_gain_q12(4096),
      mix_q15(12288),
      tone_alpha_q15(12288),
      lp_state(0),
      hp_prev_in(0),
      hp_prev_out(0) {
    set_params(320, 18000, 12288);
}

void Distortion::set_params(int drive, int th, int mix) {
    const int clamped_drive = clamp_int(drive, 0, 1000);
    const int clamped_thresh = clamp_int(th, 0, 32000);
    mix_q15 = clamp_int(mix, 0, 32000);

    // Map the generic UI controls into musically useful ranges.
    clip_threshold = 6000 +
        static_cast<int>((static_cast<int64_t>(clamped_thresh) * 20000) / 32000);
    clip_threshold_inv_q15 =
        static_cast<int>((static_cast<int64_t>(1) << 30) / clip_threshold);

    const int max_gain_q12 = 20 * 4096;
    input_gain_q12 = 4096 +
        static_cast<int>((static_cast<int64_t>(clamped_drive) * clamped_drive *
                          (max_gain_q12 - 4096)) / 1000000);

    // Keep output level more consistent as threshold drops.
    makeup_gain_q12 =
        static_cast<int>((static_cast<int64_t>(26000) << 12) / clip_threshold);

    // Darker tone when the threshold is low keeps the result smoother.
    tone_alpha_q15 = 6144 +
        static_cast<int>((static_cast<int64_t>(clamped_thresh) * 8192) / 32000);
}

void Distortion::process(int16_t* buffer, int buffer_size) {
    for (int i = 0; i < buffer_size; ++i) {
        int32_t dry = buffer[i];

        // Simple DC blocker / bass tightening before the waveshaper.
        const int32_t hp =
            dry - hp_prev_in + static_cast<int32_t>((static_cast<int64_t>(hp_prev_out) * 32604) >> 15);
        hp_prev_in = dry;
        hp_prev_out = hp;

        const int32_t driven =
            static_cast<int32_t>((static_cast<int64_t>(hp) * input_gain_q12) >> 12);
        const int32_t normalized_q15 =
            static_cast<int32_t>((static_cast<int64_t>(driven) * clip_threshold_inv_q15) >> 15);

        int32_t shaped =
            static_cast<int32_t>((static_cast<int64_t>(cubic_soft_clip_q15(normalized_q15)) *
                                  clip_threshold * 3) >> 16);
        shaped = static_cast<int32_t>((static_cast<int64_t>(shaped) * makeup_gain_q12) >> 12);

        lp_state += static_cast<int32_t>(
            (static_cast<int64_t>(shaped - lp_state) * tone_alpha_q15) >> 15);
        const int32_t wet = lp_state;

        const int32_t out =
            ((dry * (32768 - mix_q15)) + (wet * mix_q15)) >> 15;

        buffer[i] = clamp_i16(out);
    }
}

void Distortion::reset() {
    lp_state = 0;
    hp_prev_in = 0;
    hp_prev_out = 0;
}
