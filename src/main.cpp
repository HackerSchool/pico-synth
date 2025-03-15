#include <math.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/pll.h"
#include "hardware/structs/clocks.h"

#include "pico/audio.h"

#include "quadrature_encoder.pio.h"

#include "i2s_init.hpp"
#include "Wavetable.hpp"

#define SINE_WAVE_TABLE_LEN 2048

uint32_t step0 = 0x200000;
uint32_t step1 = 0x200000;
uint32_t pos0 = 0;
uint32_t pos1 = 0;
const uint32_t pos_max = 0x10000 * SINE_WAVE_TABLE_LEN;
uint vol = 100;

int main() {
    int new_value, delta, old_value = 0;
    int last_value = -1, last_delta = -1;
    int tempo = 100; // Base tempo (100% speed)

    // Quadrature encoder setup
    const uint PIN_AB = 10;
    stdio_init_all();
    printf("Hello from quadrature encoder\n");

    PIO pio = pio0;
    const uint sm = 0;
    pio_add_program(pio, &quadrature_encoder_program);
    quadrature_encoder_program_init(pio, sm, PIN_AB, 0);

    // Set up system clock for better audio
    pll_init(pll_usb, 1, 1536 * MHZ, 4, 4);
    clock_configure(clk_usb, 0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    96 * MHZ, 48 * MHZ);
    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, 96 * MHZ,
                    96 * MHZ);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    96 * MHZ, 96 * MHZ);
    stdio_init_all();

    // Enable less noise in audio output
    gpio_init(PIN_DCDC_PSM_CTRL);
    gpio_set_dir(PIN_DCDC_PSM_CTRL, GPIO_OUT);
    gpio_put(PIN_DCDC_PSM_CTRL, 1);

    // Initialize I2S audio output
    ap = i2s_audio_init(44100);

    // Define the melody (first notes of "The Star-Spangled Banner")
    struct Note {
        int frequency;
        int duration;
    };

    Note melody[] = {
        {659, 200}, // E5
        {587, 200}, // D5
        {740, 200}, // F#5
        {830, 200}, // G#5
        {554, 200}, // C#5
        {494, 200}, // B4
        {587, 200}, // D5
        {659, 400}, // E5 (longer)
        {494, 200}, // B4
        {440, 200}, // A4
        {554, 200}, // C#5
        {659, 400}, // E5 (longer)
        {440, 200}, // A4
        {0, 200},   // Pause
        {330, 400}, // E4
        {0, 200}    // Pause
    };

    int melody_length = sizeof(melody) / sizeof(Note);

    // Function to set the sine wave frequency
    auto set_note_frequency = [](int freq) {
        if (freq == 0) {
            step0 = 0; // Mute sound for pauses
        } else {
            step0 = (freq * SINE_WAVE_TABLE_LEN * 0x20000) / 44100 * 16;
        }
    };

    while (true) {
        // Read encoder for tempo adjustment
        new_value = quadrature_encoder_get_count(pio, sm);
        delta = new_value - old_value;
        old_value = new_value;

        if (new_value != last_value || delta != last_delta) {
            printf("position %8d, delta %6d\n", new_value, delta);
            last_value = new_value;
            last_delta = delta;

            // Adjust tempo (speed up/down playback)
            if (delta < 0 && tempo > 50) {
                tempo -= 5; // Decrease tempo (slower)
            } else if (delta > 0 && tempo < 200) {
                tempo += 5; // Increase tempo (faster)
            }
        }

        // Play melody
        for (int i = 0; i < melody_length; i++) {
            set_note_frequency(melody[i].frequency);
            sleep_ms(melody[i].duration * 100 / tempo);
        }

        // Reset to silence after melody
        set_note_frequency(0);
        sleep_ms(500);
    }

    return 0;
}

void decode() {
    audio_buffer_t *buffer = take_audio_buffer(ap, false);
    if (buffer == NULL) {
        return;
    }
    int32_t *samples = (int32_t *)buffer->buffer->bytes;
    for (uint i = 0; i < buffer->max_sample_count; i++) {
        int32_t value0 = (vol * sine_wave_table[pos0 >> 16u]) << 8u;
        int32_t value1 = (vol * sine_wave_table[pos1 >> 16u]) << 8u;
        // use 32bit full scale
        samples[i * 2 + 0] = value0 + (value0 >> 16u); // L
        samples[i * 2 + 1] = value1 + (value1 >> 16u); // R
        pos0 += step0;
        pos1 += step1;
        if (pos0 >= pos_max)
            pos0 -= pos_max;
        if (pos1 >= pos_max)
            pos1 -= pos_max;
    }
    buffer->sample_count = buffer->max_sample_count;
    give_audio_buffer(ap, buffer);
    return;
}

extern "C" {
// callback from:
//   void __isr __time_critical_func(audio_i2s_dma_irq_handler)()
//   defined at my_pico_audio_i2s/audio_i2s.c
//   where i2s_callback_func() is declared with __attribute__((weak))
void i2s_callback_func() {
    if (decode_flg) {
        decode();
    }
}
}
