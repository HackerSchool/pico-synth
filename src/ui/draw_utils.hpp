#ifndef DRAW_UTILS_HPP
#define DRAW_UTILS_HPP

#include "ssd1306.h"
#include "../fonts/font.h"
#include <array>
#include <cstdint>
#include "Wavetable.hpp"

void ssd1306_draw_string_inverted(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, const char *s);

const std::array<short int, 2048> &get_wavetable_for_channel(int channel);

void draw_waveform(ssd1306_t *display,
                   const std::array<int16_t, WAVE_TABLE_LEN> &table, uint16_t x,
                   uint16_t y, // top-left corner
                   uint16_t width, uint16_t height);

#endif // DRAW_UTILS_H
