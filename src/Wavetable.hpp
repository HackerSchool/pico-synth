#ifndef WAVETABLE
#define WAVETABLE

#include <array>
#include <cstdint>

#define WAVE_TABLE_LEN 512

enum WaveType { Sine, Square, Triangle, Sawtooth };

extern const std::array<int16_t, WAVE_TABLE_LEN> sine_wave_table;
extern const std::array<int16_t, WAVE_TABLE_LEN> square_wave_table;
extern const std::array<int16_t, WAVE_TABLE_LEN> triangle_wave_table;
extern const std::array<int16_t, WAVE_TABLE_LEN> sawtooth_wave_table;

#endif // !WAVETABLE
