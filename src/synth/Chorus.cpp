/*
 * Chorus effect implementation:
 * - Classic modulated short-delay chorus using a shared circular delay buffer.
 * - Two delay taps are modulated by low-rate triangle LFOs with opposite phase.
 * - Fractional delay is handled with linear interpolation for smoother pitch motion.
 * - The two moving taps are averaged and blended back with the dry signal.
 */

#include "Chorus.hpp"
#include <cstddef>
#include <cstring>

namespace {
constexpr size_t kChorusSamples = 4096;
constexpr size_t kChorusMask = kChorusSamples - 1;
}

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

static inline int16_t triangle_lfo_q15(uint32_t phase) {
    const uint32_t ramp = phase >> 16;
    const uint32_t tri = (ramp < 32768U) ? ramp : (65535U - ramp);
    return static_cast<int16_t>((static_cast<int32_t>(tri) << 1) - 32767);
}

static inline int16_t read_delay_q8(const std::array<int16_t, kChorusSamples>& buf,
                                    size_t write_idx,
                                    int delay_q8) {
    const size_t delay_int = static_cast<size_t>(delay_q8 >> 8);
    const uint32_t frac = static_cast<uint32_t>(delay_q8 & 0xFF);
    const size_t idx_a = (write_idx - delay_int) & kChorusMask;
    const size_t idx_b = (idx_a - 1) & kChorusMask;
    const int32_t newer = buf[idx_a];
    const int32_t older = buf[idx_b];
    const int32_t interp =
        (newer * static_cast<int32_t>(256U - frac)) + (older * static_cast<int32_t>(frac));
    return static_cast<int16_t>(interp >> 8);
}

Chorus::Chorus()
    : write_idx(0),
      lfo_phase(0),
      lfo_phase_inc(0),
      tap1_base_q8(0),
      tap2_base_q8(0),
      tap1_depth_q8(0),
      tap2_depth_q8(0),
      mix_q15(8192) {
    buf.fill(0);
    set_params(320, 12000, 8192);
}

Chorus::~Chorus() {}

void Chorus::set_params(int rate, int depth, int mix) {
    const int clamped_rate = clamp_int(rate, 0, 1000);
    const int clamped_depth = clamp_int(depth, 0, 32000);
    mix_q15 = clamp_int(mix, 0, 32000);

    // Keep rate in a classic chorus range: roughly 0.12 Hz to 1.8 Hz.
    const uint32_t rate_millihz =
        120U + static_cast<uint32_t>((static_cast<int64_t>(clamped_rate) * 1680) / 1000);
    lfo_phase_inc = static_cast<uint32_t>(
        (static_cast<uint64_t>(rate_millihz) << 32) / (static_cast<uint64_t>(SAMPLE_RATE) * 1000ULL));

    // Two modulated taps with slightly different base delays help the mono sum
    // stay thick and musical without extra all-pass stages or feedback.
    const int tap1_base_samples = 10 * SAMPLE_RATE / 1000;  // 10 ms
    const int tap2_base_samples = 17 * SAMPLE_RATE / 1000;  // 17 ms
    const int min_depth_samples = SAMPLE_RATE / 1000;       // ~1 ms
    const int max_depth_samples = 6 * SAMPLE_RATE / 1000;   // ~6 ms
    const int depth_samples = min_depth_samples +
        static_cast<int>((static_cast<int64_t>(clamped_depth) *
                          (max_depth_samples - min_depth_samples)) / 32000);

    tap1_base_q8 = tap1_base_samples << 8;
    tap2_base_q8 = tap2_base_samples << 8;
    tap1_depth_q8 = depth_samples << 8;
    tap2_depth_q8 = (depth_samples * 3) << 6; // 75% of tap1 depth
}

void Chorus::process(int16_t* buffer, int buffer_size) {
    for (int i = 0; i < buffer_size; ++i) {
        int32_t in = buffer[i];

        const uint32_t phase_b = lfo_phase + 0x80000000u;
        const uint32_t lfo_a = static_cast<uint32_t>(triangle_lfo_q15(lfo_phase) + 32768);
        const uint32_t lfo_b = static_cast<uint32_t>(triangle_lfo_q15(phase_b) + 32768);

        const int tap1_delay_q8 =
            tap1_base_q8 + static_cast<int>((static_cast<int64_t>(tap1_depth_q8) * lfo_a) >> 16);
        const int tap2_delay_q8 =
            tap2_base_q8 + static_cast<int>((static_cast<int64_t>(tap2_depth_q8) * lfo_b) >> 16);

        const int32_t tap1 = read_delay_q8(buf, write_idx, tap1_delay_q8);
        const int32_t tap2 = read_delay_q8(buf, write_idx, tap2_delay_q8);
        const int32_t wet = (tap1 + tap2) >> 1;
        const int32_t out = ((in * (32768 - mix_q15)) + (wet * mix_q15)) >> 15;

        buf[write_idx] = clamp_i16(in);
        write_idx = (write_idx + 1) & CHORUS_MASK;
        lfo_phase += lfo_phase_inc;
        buffer[i] = clamp_i16(out);
    }
}

void Chorus::reset() {
    buf.fill(0);
    write_idx = 0;
    lfo_phase = 0;
}
