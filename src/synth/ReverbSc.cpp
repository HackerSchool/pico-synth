/*
 * ReverbSc-inspired reverb implementation:
 * - This is a lightweight mono adaptation of the denser "reverbsc" style
 *   topology rather than a direct DaisySP float port.
 * - The input first passes through four short all-pass diffusers to smear
 *   transients before they hit the late reverb tank.
 * - The late field uses eight modulated delay lines with a Householder-style
 *   feedback matrix, which gives a more complex and lively tail than the
 *   simpler stock reverb while staying friendly to RP2350 CPU/RAM limits.
 * - "time" controls feedback and line scaling, "tone" controls the damping in
 *   each feedback loop, and "mix" blends the wet signal back with the dry path.
 *
 * The goal here is to capture the character of the online RP2350 example in a
 * form that fits PicoSynth's existing fixed-point mono FX chain.
 */

#include "ReverbSc.hpp"

#include "config.hpp"

#include <cstring>

namespace {
constexpr int32_t kDiffuserGainQ15 = 21627; // ~0.66

constexpr float rate_to_phase_inc(float hz) {
    return (hz * 4294967296.0f) / static_cast<float>(SAMPLE_RATE);
}
} // namespace

inline int16_t ReverbScFx::clamp16(int32_t x) {
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return static_cast<int16_t>(x);
}

inline int32_t ReverbScFx::process_allpass(Allpass& ap, int32_t x) {
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

inline int32_t ReverbScFx::triangle_q15(uint32_t phase) {
    const uint32_t p = phase >> 16;
    const int32_t tri = (p < 32768u) ? static_cast<int32_t>(p)
                                     : static_cast<int32_t>(65535u - p);
    return (tri << 1) - 32767;
}

ReverbScFx::ReverbScFx() {
    for (size_t i = 0; i < NUM_DIFFUSERS; ++i) {
        diffusers[i] = {
            diffuser_bufs[i].data(), 0, DIFFUSER_DELAYS[i], DIFFUSER_MASK, kDiffuserGainQ15
        };
    }

    const float mod_rates[NUM_LINES] = {
        0.071f, 0.089f, 0.097f, 0.113f, 0.131f, 0.149f, 0.173f, 0.191f
    };
    const uint32_t mod_phases[NUM_LINES] = {
        0x00000000u, 0x20000000u, 0x40000000u, 0x60000000u,
        0x80000000u, 0xA0000000u, 0xC0000000u, 0xE0000000u
    };

    for (size_t i = 0; i < NUM_LINES; ++i) {
        lines[i] = {
            line_bufs[i].data(),
            0,
            LINE_BASE_DELAYS[i],
            0,
            mod_phases[i],
            static_cast<uint32_t>(rate_to_phase_inc(mod_rates[i])),
            0
        };
    }

    reset();
    set_params(0.78f, 0.75f, 0.34f);
}

void ReverbScFx::reset() {
    for (auto& buf : diffuser_bufs) {
        std::memset(buf.data(), 0, sizeof(buf));
    }
    for (auto& buf : line_bufs) {
        std::memset(buf.data(), 0, sizeof(buf));
    }

    for (auto& ap : diffusers) {
        ap.idx = 0;
    }

    for (auto& line : lines) {
        line.idx = 0;
        line.filterstore = 0;
    }
}

void ReverbScFx::set_params(float time, float tone, float mix) {
    if (time < 0.0f) time = 0.0f;
    if (time > 1.0f) time = 1.0f;

    if (tone < 0.0f) tone = 0.0f;
    if (tone > 1.0f) tone = 1.0f;

    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;

    const float feedback = 0.58f + time * 0.36f;
    const float tone_amount = 0.10f + tone * 0.82f;
    const float size_scale = 0.92f + time * 0.22f;
    const int16_t base_mod_depth = static_cast<int16_t>(2 + time * 5.0f);

    feedback_q15 = static_cast<int32_t>(feedback * 32767.0f);
    tone_q15 = static_cast<int32_t>(tone_amount * 32767.0f);
    mix_q15 = static_cast<int32_t>(mix * 32767.0f);

    for (size_t i = 0; i < NUM_LINES; ++i) {
        size_t scaled_delay = static_cast<size_t>(LINE_BASE_DELAYS[i] * size_scale);
        if (scaled_delay >= LINE_BUF_SIZE - 16) {
            scaled_delay = LINE_BUF_SIZE - 16;
        }
        lines[i].delay = scaled_delay;
        lines[i].mod_depth = static_cast<int16_t>(base_mod_depth + MOD_DEPTH_OFFSETS[i]);
    }
}

void ReverbScFx::process(int16_t* buffer, int buffer_size) {
    const int32_t dry_q15 = 32767 - mix_q15;
    const int32_t tone_inv_q15 = 32767 - tone_q15;

    for (int i = 0; i < buffer_size; ++i) {
        const int32_t dry_in = buffer[i];

        int32_t x = dry_in;
        for (auto& ap : diffusers) {
            x = process_allpass(ap, x);
        }

        int32_t delayed[NUM_LINES];
        int32_t filtered[NUM_LINES];
        int32_t filtered_sum = 0;

        for (size_t line_index = 0; line_index < NUM_LINES; ++line_index) {
            DelayLine& line = lines[line_index];
            const int32_t mod_offset =
                static_cast<int32_t>((static_cast<int64_t>(triangle_q15(line.mod_phase)) * line.mod_depth) >> 15);
            int32_t read_delay = static_cast<int32_t>(line.delay) + mod_offset;
            if (read_delay < 1) {
                read_delay = 1;
            } else if (read_delay >= static_cast<int32_t>(LINE_BUF_SIZE)) {
                read_delay = static_cast<int32_t>(LINE_BUF_SIZE) - 1;
            }

            const size_t read_idx = (line.idx - static_cast<size_t>(read_delay)) & LINE_MASK;
            delayed[line_index] = line.buf[read_idx];
            filtered[line_index] = static_cast<int32_t>(
                ((static_cast<int64_t>(delayed[line_index]) * tone_q15) +
                 (static_cast<int64_t>(line.filterstore) * tone_inv_q15)) >> 15
            );
            line.filterstore = filtered[line_index];
            filtered_sum += filtered[line_index];
            line.mod_phase += line.mod_rate;
        }

        const int32_t householder = filtered_sum >> 2; // (2 / 8) * sum

        for (size_t line_index = 0; line_index < NUM_LINES; ++line_index) {
            DelayLine& line = lines[line_index];
            const int32_t excitation = (INPUT_POLARITY[line_index] > 0) ? x : -x;
            const int32_t feedback_signal = static_cast<int32_t>(
                (static_cast<int64_t>(householder - filtered[line_index]) * feedback_q15) >> 15
            );
            line.buf[line.idx] = clamp16(excitation + feedback_signal);
            line.idx = (line.idx + 1) & LINE_MASK;
        }

        int32_t wet =
            delayed[0] - filtered[1] + delayed[2] + filtered[3] -
            delayed[4] + filtered[5] + delayed[6] - filtered[7];
        wet >>= 3;
        wet = static_cast<int32_t>((static_cast<int64_t>(wet) * 26214) >> 15); // ~0.8 trim

        const int32_t out = static_cast<int32_t>(
            ((static_cast<int64_t>(dry_in) * dry_q15) +
             (static_cast<int64_t>(wet) * mix_q15)) >> 15
        );

        buffer[i] = clamp16(out);
    }
}
