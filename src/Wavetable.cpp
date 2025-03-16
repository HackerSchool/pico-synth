#include "Wavetable.hpp"
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
            table[i] = static_cast<int16_t>(-4 * 32767 * (phase - 0.5f) + 32767);
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
