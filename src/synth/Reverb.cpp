/*
 * Reverb effect implementation:
 * - Schroeder/Freeverb-style structure adapted for lightweight fixed-point use.
 * - Input first passes through a short pre-delay and a chain of all-pass diffusers.
 * - The late field uses four modulated feedback delay lines with one-pole damping.
 * - A simple feedback matrix and mixed taps create the final dense wet reverb tail.
 */

#include "Reverb.hpp"

#include "config.hpp"

#include <cstring>

namespace {
constexpr int32_t DIFFUSER_GAIN_Q15 = 23593; // ~0.72

constexpr float rate_to_phase_inc(float hz) {
    return (hz * 4294967296.0f) / static_cast<float>(SAMPLE_RATE);
}
} // namespace

inline int16_t Reverb::clamp16(int32_t x) {
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return static_cast<int16_t>(x);
}

inline int32_t Reverb::process_allpass(Allpass& ap, int32_t x) {
    const size_t read_idx = (ap.idx - ap.delay) & ap.mask;
    const int32_t bufout = ap.buf[read_idx];
    const int32_t y = bufout - static_cast<int32_t>((static_cast<int64_t>(ap.gain_q15) * x) >> 15);

    ap.buf[ap.idx] = clamp16(
        static_cast<int32_t>(
            static_cast<int64_t>(x) + ((static_cast<int64_t>(ap.gain_q15) * y) >> 15)
        )
    );

    ap.idx = (ap.idx + 1) & ap.mask;
    return y;
}

inline int32_t Reverb::triangle_q15(uint32_t phase) {
    const uint32_t p = phase >> 16;
    const int32_t tri = (p < 32768u) ? static_cast<int32_t>(p)
                                     : static_cast<int32_t>(65535u - p);
    return (tri << 1) - 32767;
}

Reverb::Reverb() {
    diffusers[0] = {diffuser_buf1.data(), 0, DIFFUSER_DELAYS[0], DIFFUSER_MASK, DIFFUSER_GAIN_Q15};
    diffusers[1] = {diffuser_buf2.data(), 0, DIFFUSER_DELAYS[1], DIFFUSER_MASK, DIFFUSER_GAIN_Q15};
    diffusers[2] = {diffuser_buf3.data(), 0, DIFFUSER_DELAYS[2], DIFFUSER_MASK, DIFFUSER_GAIN_Q15};

    lines[0] = {line_buf1.data(), 0, LINE_BASE_DELAYS[0], 0, 0x00000000u,
                static_cast<uint32_t>(rate_to_phase_inc(0.071f)), 0};
    lines[1] = {line_buf2.data(), 0, LINE_BASE_DELAYS[1], 0, 0x40000000u,
                static_cast<uint32_t>(rate_to_phase_inc(0.097f)), 0};
    lines[2] = {line_buf3.data(), 0, LINE_BASE_DELAYS[2], 0, 0x80000000u,
                static_cast<uint32_t>(rate_to_phase_inc(0.133f)), 0};
    lines[3] = {line_buf4.data(), 0, LINE_BASE_DELAYS[3], 0, 0xC0000000u,
                static_cast<uint32_t>(rate_to_phase_inc(0.181f)), 0};

    reset();
    set_params(0.75f, 0.30f, 0.35f);
}

void Reverb::reset() {
    std::memset(pre_delay_buf.data(), 0, sizeof(pre_delay_buf));
    std::memset(diffuser_buf1.data(), 0, sizeof(diffuser_buf1));
    std::memset(diffuser_buf2.data(), 0, sizeof(diffuser_buf2));
    std::memset(diffuser_buf3.data(), 0, sizeof(diffuser_buf3));

    std::memset(line_buf1.data(), 0, sizeof(line_buf1));
    std::memset(line_buf2.data(), 0, sizeof(line_buf2));
    std::memset(line_buf3.data(), 0, sizeof(line_buf3));
    std::memset(line_buf4.data(), 0, sizeof(line_buf4));

    pre_delay_idx = 0;

    for (auto& ap : diffusers) {
        ap.idx = 0;
    }

    for (auto& line : lines) {
        line.idx = 0;
        line.filterstore = 0;
    }
}

void Reverb::set_params(float room_size, float damp, float mix) {
    if (room_size < 0.0f) room_size = 0.0f;
    if (room_size > 1.0f) room_size = 1.0f;

    if (damp < 0.0f) damp = 0.0f;
    if (damp > 1.0f) damp = 1.0f;

    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;

    const float feedback = 0.70f + room_size * 0.23f;
    const float damping = 0.06f + damp * 0.46f;
    const float size_scale = 0.78f + room_size * 0.40f;
    const float pre_delay_ms = 6.0f + room_size * 30.0f;
    const int16_t base_mod_depth = static_cast<int16_t>(3 + room_size * 8.0f);

    feedback_q15 = static_cast<int32_t>(feedback * 32767.0f);
    damp_q15 = static_cast<int32_t>(damping * 32767.0f);
    mix_q15 = static_cast<int32_t>(mix * 32767.0f);

    pre_delay_samples = static_cast<size_t>((pre_delay_ms * SAMPLE_RATE) / 1000.0f);
    if (pre_delay_samples >= PRE_DELAY_BUF_SIZE) {
        pre_delay_samples = PRE_DELAY_BUF_SIZE - 1;
    }

    for (size_t i = 0; i < NUM_LINES; ++i) {
        size_t scaled_delay = static_cast<size_t>(LINE_BASE_DELAYS[i] * size_scale);
        if (scaled_delay >= LINE_BUF_SIZE - 32) {
            scaled_delay = LINE_BUF_SIZE - 32;
        }
        lines[i].delay = scaled_delay;
        lines[i].mod_depth = base_mod_depth + LINE_MOD_DEPTH_OFFSETS[i];
    }
}

void Reverb::process(int16_t* buffer, int buffer_size) {
    const int32_t dry_q15 = 32767 - mix_q15;
    const int32_t damp_inv_q15 = 32767 - damp_q15;

    for (int i = 0; i < buffer_size; ++i) {
        const int32_t dry_in = buffer[i];

        const size_t pre_read_idx = (pre_delay_idx - pre_delay_samples) & PRE_DELAY_MASK;
        int32_t x = pre_delay_buf[pre_read_idx];
        pre_delay_buf[pre_delay_idx] = clamp16(dry_in);
        pre_delay_idx = (pre_delay_idx + 1) & PRE_DELAY_MASK;

        x <<= 1;

        x = process_allpass(diffusers[0], x);
        x = process_allpass(diffusers[1], x);
        x = process_allpass(diffusers[2], x);

        int32_t delayed[NUM_LINES];
        int32_t filtered[NUM_LINES];

        for (size_t line_index = 0; line_index < NUM_LINES; ++line_index) {
            DelayLine& line = lines[line_index];
            const int32_t mod_offset =
                static_cast<int32_t>((static_cast<int64_t>(triangle_q15(line.mod_phase)) * line.mod_depth) >> 15);
            // Guard against index underflow: add buffer size before subtracting to handle wraparound
            const int32_t delay_with_mod = static_cast<int32_t>(line.delay) + mod_offset;
            const size_t read_idx =
                (line.idx + (LINE_BUF_SIZE - static_cast<size_t>(delay_with_mod))) & LINE_MASK;

            delayed[line_index] = line.buf[read_idx];
            filtered[line_index] = static_cast<int32_t>(
                ((static_cast<int64_t>(delayed[line_index]) * damp_inv_q15) +
                 (static_cast<int64_t>(line.filterstore) * damp_q15)) >> 15
            );
            line.filterstore = filtered[line_index];
            line.mod_phase += line.mod_rate;
        }

        const int32_t filtered_sum =
            filtered[0] + filtered[1] + filtered[2] + filtered[3];
        const int32_t matrix_mix = filtered_sum >> 1;

        lines[0].buf[lines[0].idx] = clamp16(
            x + static_cast<int32_t>((static_cast<int64_t>(matrix_mix - filtered[0]) * feedback_q15) >> 15)
        );
        lines[1].buf[lines[1].idx] = clamp16(
            x + static_cast<int32_t>((static_cast<int64_t>(matrix_mix - filtered[1]) * feedback_q15) >> 15)
        );
        lines[2].buf[lines[2].idx] = clamp16(
            -x + static_cast<int32_t>((static_cast<int64_t>(matrix_mix - filtered[2]) * feedback_q15) >> 15)
        );
        lines[3].buf[lines[3].idx] = clamp16(
            -x + static_cast<int32_t>((static_cast<int64_t>(matrix_mix - filtered[3]) * feedback_q15) >> 15)
        );

        for (auto& line : lines) {
            line.idx = (line.idx + 1) & LINE_MASK;
        }

        int32_t wet = (filtered[0] + delayed[1] + filtered[2] + delayed[3]) >> 2;
        wet = static_cast<int32_t>((static_cast<int64_t>(wet) * 29491) >> 15); // ~0.9 trim

        const int32_t out = static_cast<int32_t>(
            ((static_cast<int64_t>(dry_in) * dry_q15) +
             (static_cast<int64_t>(wet) * mix_q15)) >> 15
        );

        buffer[i] = clamp16(out);
    }
}
