#ifndef WAVETABLE
#define WAVETABLE

#include <array>
#include <cstdint>
#include "fixed_point.h"

#define WAVE_TABLE_LEN 512
#define FILTER_ORDER 33


enum WaveType { Sine, Square, Triangle, Sawtooth, Sinc};

extern const std::array<int16_t, WAVE_TABLE_LEN> sine_wave_table;
extern const std::array<int16_t, WAVE_TABLE_LEN> square_wave_table;
extern const std::array<int16_t, WAVE_TABLE_LEN> triangle_wave_table;
extern const std::array<int16_t, WAVE_TABLE_LEN> sawtooth_wave_table;
extern const std::array<int16_t, WAVE_TABLE_LEN> sinc_table;


extern const std::array<q8_24_t, WAVE_TABLE_LEN> sinc_table_fp;
extern const std::array<q8_24_t, FILTER_ORDER> hanning_window_table_fp;

#endif // !WAVETABLE
