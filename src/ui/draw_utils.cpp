#include "draw_utils.hpp"
#include "Wavetable.hpp"
#include "Synth.hpp"

void ssd1306_draw_string_inverted(ssd1306_t *p, uint32_t x, uint32_t y, uint32_t scale, const char *s) {
    const uint8_t *font = font_8x5;  // or use another font
    uint32_t char_w = (font[1] + font[2]) * scale;
    uint32_t char_h = font[0] * scale;

    for (int32_t x_n = x; *s; x_n += char_w, ++s) {
        // draw background box
        ssd1306_draw_square(p, x_n, y, char_w, char_h);

        // draw character, but "cleared" instead of set
        if (*s < font[3] || *s > font[4]) continue;

        uint32_t parts_per_line = (font[0] >> 3) + ((font[0] & 7) > 0);
        for (uint8_t w = 0; w < font[1]; ++w) {
            uint32_t pp = (*s - font[3]) * font[1] * parts_per_line + w * parts_per_line + 5;
            for (uint32_t lp = 0; lp < parts_per_line; ++lp) {
                uint8_t line = font[pp];
                for (int8_t j = 0; j < 8; ++j, line >>= 1) {
                    if (line & 1) {
                        // clear pixels instead of drawing
                        ssd1306_clear_square(p, x_n + w * scale, y + ((lp << 3) + j) * scale, scale, scale);
                    }
                }
                ++pp;
            }
        }
    }
}

const std::array<short int, 2048>& get_wavetable_for_channel(int channel) {
    // Clamp channel to valid range (0-15)
    if (channel < 0 || channel >= 16) {
        return sine_wave_table; // Default fallback
    }
    
    // Get the wave type for this channel
    WaveType wave_type = channel_wave_map[channel];
    
    // Return the appropriate wavetable
    switch (wave_type) {
    case Sine:
        return sine_wave_table;
    case Square:
        return square_wave_table;
    case Triangle:
        return triangle_wave_table;
    case Sawtooth:
        return sawtooth_wave_table;
    case Sinc:
        return sinc_table;
    default:
        return sine_wave_table;
    }
}

// Draws a waveform on the SSD1306 display
void draw_waveform(ssd1306_t *display,
                   const std::array<int16_t, WAVE_TABLE_LEN> &table,
                   uint16_t x, uint16_t y,   // top-left corner
                   uint16_t width, uint16_t height) {

    const int16_t midline = static_cast<int16_t>(y + height / 2);
    const size_t samples = width; // one pixel per x column

    for (size_t i = 0; i < samples - 1; ++i) {
        // Sample two consecutive points
        size_t index1 = (i * WAVE_TABLE_LEN) / samples;
        size_t index2 = ((i + 1) * WAVE_TABLE_LEN) / samples;

        int16_t value1 = table[index1];
        int16_t value2 = table[index2];

        // Scale from [-32767, 32767] to [0, height]
        int16_t y1 = static_cast<int16_t>(midline - (value1 * ((height-1) / 2)) / 32767);
        int16_t y2 = static_cast<int16_t>(midline - (value2 * ((height-1) / 2)) / 32767);

        // Draw line from (x + i, y1) to (x + i + 1, y2)
        // Removed the boolean parameter - ssd1306_draw_line only takes 5 parameters
        ssd1306_draw_line(display, x + i, y1, x + i + 1, y2);
    }
}