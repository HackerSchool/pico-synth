#include "Compressor.hpp"

namespace {
inline int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

inline int16_t clamp_i16(int32_t value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return static_cast<int16_t>(value);
}

inline int32_t abs_i32(int32_t value) {
    return value < 0 ? -value : value;
}
} // namespace

Compressor::Compressor() {
    set_params(420, 6000, 32000);
}

void Compressor::set_params(int threshold, int makeup, int mix) {
    const int clamped_threshold = clamp_int(threshold, 0, 1000);
    const int clamped_makeup = clamp_int(makeup, 0, 32000);

    threshold_level =
        2048 + static_cast<int32_t>((static_cast<int64_t>(clamped_threshold) * 22000) / 1000);
    makeup_gain_q12 =
        4096 + static_cast<int32_t>((static_cast<int64_t>(clamped_makeup) * (2 * 4096)) / 32000);
    mix_q15 = clamp_int(mix, 0, 32000);
}

void Compressor::process(int16_t *buffer, int buffer_size) {
    static constexpr int kAttackDivisor = 8;
    static constexpr int kReleaseDivisor = 128;

    for (int i = 0; i < buffer_size; ++i) {
        const int32_t dry = buffer[i];
        const int32_t level = abs_i32(dry);
        const int32_t delta = level - envelope;

        if (delta > 0) {
            int32_t step = delta / kAttackDivisor;
            if (step == 0) step = 1;
            envelope += step;
        } else if (delta < 0) {
            int32_t step = delta / kReleaseDivisor;
            if (step == 0) step = -1;
            envelope += step;
        }

        int32_t gain_q15 = 32767;
        if (envelope > threshold_level) {
            const int32_t excess = envelope - threshold_level;
            const int32_t compressed_level = threshold_level + (excess / 4);
            gain_q15 = static_cast<int32_t>(
                (static_cast<int64_t>(compressed_level) << 15) /
                (envelope > 0 ? envelope : 1));
        }

        int32_t wet = static_cast<int32_t>((static_cast<int64_t>(dry) * gain_q15) >> 15);
        wet = static_cast<int32_t>((static_cast<int64_t>(wet) * makeup_gain_q12) >> 12);

        const int32_t output =
            ((dry * (32767 - mix_q15)) + (wet * mix_q15)) >> 15;
        buffer[i] = clamp_i16(output);
    }
}

void Compressor::reset() {
    envelope = 0;
}
