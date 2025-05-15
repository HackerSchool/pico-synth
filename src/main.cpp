#include <array>
#include <cstdint>
#include <stdio.h>

#include "config.hpp"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/pll.h"
#include "hardware/structs/clocks.h"

#include "pico/audio.h"

#include "fixed_point.h"
#include "tusb.h"
// #include "tusb_config.h"
//
#include "ssd1306.h"
#include <fpm/fixed.hpp> // For fpm::fixed_16_16

#include "quadrature_encoder.pio.h"

#include "Envelope.hpp"
#include "HardwareManager.hpp"
#include "MidiHandler.hpp"
#include "Oscillator.hpp"
#include "Synth.hpp"
#include "Wavetable.hpp"
#include "i2s_init.hpp"

uint vol = 100;

// Double output buffer
std::array<int16_t, SAMPLES_PER_BUFFER> out1 = {};
std::array<int16_t, SAMPLES_PER_BUFFER> out2 = {};
bool write_flag = 0;
bool buff = 0;

void setup_gpios(void) {
    i2c_init(i2c1, 400000);
    gpio_set_function(26, GPIO_FUNC_I2C);
    gpio_set_function(27, GPIO_FUNC_I2C);
    gpio_pull_up(26);
    gpio_pull_up(27);

    i2c_init(i2c0, 100 * 1000);
    gpio_set_function(20, GPIO_FUNC_I2C); // SDA
    gpio_set_function(21, GPIO_FUNC_I2C); // SCL
    gpio_pull_up(20);
    gpio_pull_up(21);
}

int main() {
    // Set up system clock for better audio
    pll_init(pll_usb, 1, 1536 * MHZ, 4, 4);
    clock_configure(clk_usb, 0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    96 * MHZ, 48 * MHZ);
    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    120.3 * MHZ, 120.3 * MHZ);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    120.3 * MHZ, 120.3 * MHZ);

    stdio_init_all();
    stdio_usb_init();

    // Initialize TinyUSB
    tusb_init();

    // Enable less noise in audio output
    gpio_init(PIN_DCDC_PSM_CTRL);
    gpio_set_dir(PIN_DCDC_PSM_CTRL, GPIO_OUT);
    gpio_put(PIN_DCDC_PSM_CTRL, 1);

    // Initialize I2S audio output
    ap = i2s_audio_init(44100);

    setup_gpios();

    const char *words[] = {"SSD1306", "DISPLAY", "DRIVER"};

    ssd1306_t disp;

    disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);

    // ssd1306_hflip(&disp, 1);
    ssd1306_rotate(&disp, 1);
    ssd1306_clear(&disp);

    Synth synth = Synth();

    MidiHandler midi_handler = MidiHandler(synth);

    ssd1306_draw_string(&disp, 8, 24, 1, words[0]);
    ssd1306_show(&disp);

    uint16_t prev_state = 0;

    for (int i = 0; i < NUM_ENCODERS; ++i) {
        init_encoder(&encoders[i]);
    }
    const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                                 66, 68, 70, -1, 67, 69, 71, 72};

    while (true) {

        // Handle USB tasks
        tud_task();

        // Handle MIDI messages
        midi_handler.midi_task();

        // encoders
        for (int i = 0; i < NUM_ENCODERS; ++i) {
            Encoder *enc = &encoders[i];
            int32_t count = quadrature_encoder_get_count(enc->pio, enc->sm);
            int32_t delta = count - enc->last_count;

            if (delta != 0) {
                printf("Encoder %d: count = %ld (delta %ld)\n", i, count,
                       delta);
                enc->last_count = count;
            }

            if (!gpio_get(enc->sw_pin)) {
                printf("Encoder %d: 🔘 button pressed\n", i);
            }
        }

        static std::bitset<128> last_state;

        if (synth.get_notes_bitmask() != last_state) {
            last_state = synth.get_notes_bitmask();
            const char *notes_str = synth.get_notes_playing_names();
            ssd1306_clear(&disp);
            ssd1306_draw_string(&disp, 8, 24, 1, notes_str);
            ssd1306_show(&disp);
        }

        uint16_t curr_state = scan_key_state(i2c0);

        update_leds_from_keys(i2c1, prev_state, curr_state);
        KeyChanges key_changes = compute_key_changes(prev_state, curr_state);

        for (int i = 0; i < 16; ++i) {
            // if its 1
            if ((key_changes.note_on_mask & (1 << i))) {

                uint8_t note = key_to_midi[i];
                if (note != 255) {
                    synth.note_on(note, 127);
                }
            }
            if ((key_changes.note_off_mask & (1 << i))) {

                uint8_t note = key_to_midi[i];

                if (note != 255) {
                    synth.note_off(note, 0);
                }
            }
        }

        prev_state = curr_state;

        int c = getchar_timeout_us(0);
        if (c >= 0) {
            if (c == '-' && vol)
                vol--;
            if ((c == '=' || c == '+') && vol < 256)
                vol++;
            // if (c == 's')
            // env1.set_trigger(5.0);
            if (c == 'p') {

                // env1.set_trigger(0.0);
                synth.low_pass.recalculate_coefficients();
                for (int i = 0; i < 33; i++) {
                    printf("h = %f\n\r", q24_to_float(synth.low_pass.h[i]));
                }
            }
            if (c == 'q')
                for (int i = 0; i < 512; i++) {
                    printf("%f,\n\r", q24_to_float(sinc_table_fp[i]));
                }
            printf("Yo\n\r");
        }
        if (write_flag) {
            if (buff) {
                synth.out();
                out1 = synth.get_output();
                write_flag = 0;
            } else {
                synth.out();
                out1 = synth.get_output();
                write_flag = 0;
            }
        }

        else {
            // without this print the thing does not work,
            // main loop runs too fast atm, no time for interrupt
            // is what Im assuming
            // thats a good thing at least
            // printf("Not writing anything\n");
            // __wfe(); // Wait for event (low power waiting)
        }
    }

    return 0;
}

void decode() {
    audio_buffer_t *buffer = take_audio_buffer(ap, false);
    if (buffer == NULL) {
        return;
    }
    int32_t *samples = (int32_t *)buffer->buffer->bytes;
    std::array<int16_t, SAMPLES_PER_BUFFER> &out = (buff) ? out1 : out1;
    for (uint i = 0; i < buffer->max_sample_count; i++) {
        int32_t value0 = (vol * out[i]) << 8u;
        int32_t value1 = (vol * out[i]) << 8u;
        // use 32bit full scale
        samples[i * 2 + 0] = value0 + (value0 >> 16u); // L
        samples[i * 2 + 1] = value1 + (value1 >> 16u); // R
    }
    buff = !buff;
    write_flag = 1;
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
