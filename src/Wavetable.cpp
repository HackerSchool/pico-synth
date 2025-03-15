#include "Wavetable.hpp"
#include "math.h"

const std::array<int16_t, WAVE_TABLE_LEN> sine_wave_table{[]() {
    std::array<int16_t, WAVE_TABLE_LEN> table{};
    for (int i = 0; i < WAVE_TABLE_LEN; i++) {
        table[i] =
            static_cast<int16_t>(32767 * cos(i * 2 * M_PI / WAVE_TABLE_LEN));
    }
    return table;
}()};


