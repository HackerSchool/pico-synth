#include "Wavetable.hpp"
#include "fixed_point.h"
#include "math.h"

// This method allows to define the wavetable at compile time!
// Sine Wavetable
const std::array<int16_t, WAVE_TABLE_LEN> sine_wave_table{[]() {
    std::array<int16_t, WAVE_TABLE_LEN> table{};
    for (int i = 0; i < WAVE_TABLE_LEN; i++) {
        table[i] =
            static_cast<int16_t>(32767 * sin(i * 2 * M_PI / WAVE_TABLE_LEN));
    }
    return table;
}()};

// Sinc Wavetable N = 32
const std::array<int16_t, WAVE_TABLE_LEN> sinc_table{[]() {
    std::array<int16_t, WAVE_TABLE_LEN> table{};
    int N = 32; // Window size
    for (int i = 0; i < WAVE_TABLE_LEN; i++) {
        // Handle the special case at i=0 to avoid division by zero
        if (i == 0) {
            table[i] = static_cast<int16_t>(
                32767); // sinc(0) = 1, scaled to int16_t range
        } else {
            float x = static_cast<float>(i) / N;
            table[i] = static_cast<int16_t>(32767 * sin(M_PI * x) / (M_PI * x));
        }
    }
    return table;
}()};

// Square Wavetable
const std::array<int16_t, WAVE_TABLE_LEN> square_wave_table{[]() {
    std::array<int16_t, WAVE_TABLE_LEN> table{};
    for (int i = 0; i < WAVE_TABLE_LEN; i++) {
        table[i] = (i < WAVE_TABLE_LEN / 2) ? 32767 : -32767;
    }
    return table;
}()};

// Triangle Wavetable
const std::array<int16_t, WAVE_TABLE_LEN> triangle_wave_table{[]() {
    std::array<int16_t, WAVE_TABLE_LEN> table{};
    for (int i = 0; i < WAVE_TABLE_LEN; i++) {
        float phase = static_cast<float>(i) / WAVE_TABLE_LEN;
        if (phase < 0.5f) {
            table[i] = static_cast<int16_t>(4 * 32767 * phase - 32767);
        } else {
            table[i] =
                static_cast<int16_t>(-4 * 32767 * (phase - 0.5f) + 32767);
        }
    }
    return table;
}()};

// Sawtooth Wavetable
const std::array<int16_t, WAVE_TABLE_LEN> sawtooth_wave_table{[]() {
    std::array<int16_t, WAVE_TABLE_LEN> table{};
    for (int i = 0; i < WAVE_TABLE_LEN; i++) {
        float phase = static_cast<float>(i) / WAVE_TABLE_LEN;
        table[i] = static_cast<int16_t>(2 * 32767 * phase - 32767);
    }
    return table;
}()};

// Sinc Wavetable N = 32
const std::array<q8_24_t, WAVE_TABLE_LEN> sinc_table_fp{[]() {
    std::array<q8_24_t, WAVE_TABLE_LEN> table{};
    int N = 32; // Window size
    for (int i = 0; i < WAVE_TABLE_LEN; i++) {
        // Handle the special case at i=0 to avoid division by zero
        if (i == 0) {
            table[i] = Q24_ONE; // sinc(0) = 1, scaled to int16_t range
        } else {
            float x = static_cast<float>(i) * N / WAVE_TABLE_LEN;
            table[i] = q24_from_float(
                static_cast<float>((sin(M_PI * x) / (M_PI * x))));
        }
    }
    return table;
}()};

// Hanning Window Wavetable N = 32
const std::array<q8_24_t, FILTER_ORDER> hanning_window_table_fp{[]() {
    std::array<q8_24_t, FILTER_ORDER> table{};
    for (int i = 0; i < FILTER_ORDER; i++) {
        // Hanning window formula: 0.5 * (1 - cos(2π*n/(N-1)))
        float value = static_cast<float>(
            0.5f * (1.0f - cos(2.0f * M_PI * i / (FILTER_ORDER - 1.0f))));
        table[i] = q24_from_float(value); // Scale to int16_t range
    }
    return table;
}()};
