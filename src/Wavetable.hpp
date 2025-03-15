#ifndef WAVETABLE
#define WAVETABLE

#include <array>
#include <cstdint>

#define WAVE_TABLE_LEN 512

extern const std::array<int16_t, WAVE_TABLE_LEN> sine_wave_table;

#endif // !WAVETABLE
