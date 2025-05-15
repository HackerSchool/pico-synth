#include <array>
#include <cstdint>
#include <stdio.h>

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
#include "MidiHandler.hpp"
#include "Oscillator.hpp"
#include "Synth.hpp"
#include "Wavetable.hpp"
#include "i2s_init.hpp"

uint vol = 100;

// Double output buffer
std::array<int16_t, 1156> out1 = {};
std::array<int16_t, 1156> out2 = {};
bool write_flag = 0;
bool buff = 0;

#define PCF8574_KEYPAD_ADDR 0x20
#define PCF8574_LED_ADDR_1 0x20
#define PCF8574_LED_ADDR_2 0x21

uint8_t led_state_1 = 0xFF;
uint8_t led_state_2 = 0xFF;

const uint8_t COL_PINS[4] = {0, 1, 6, 4};
const uint8_t ROW_PINS[4] = {2, 3, 5, 7};

uint8_t LED_MAP[16] = {1, 3, 4, 7, 0, 2, 5, 6, 0, 2, 4, 6, 1, 3, 5, 7};

uint16_t scan_key_state(i2c_inst_t *i2c) {
    uint16_t state = 0;

    for (int col = 0; col < 4; col++) {
        uint8_t data = 0xFF;
        data &= ~(1 << COL_PINS[col]); // Drive this column LOW

        // Send to PCF8574
        i2c_write_blocking(i2c, PCF8574_KEYPAD_ADDR, &data, 1, true);
        sleep_us(5); // let signals settle

        // Read state
        uint8_t val;
        i2c_read_blocking(i2c, PCF8574_KEYPAD_ADDR, &val, 1, false);

        for (int row = 0; row < 4; row++) {
            if (!(val & (1 << ROW_PINS[row]))) {
                int key_index = row + col * 4;
                state |= (1 << key_index);
            }
        }
    }

    // Reset PCF to default HIGH
    uint8_t reset = 0xFF;
    i2c_write_blocking(i2c, PCF8574_KEYPAD_ADDR, &reset, 1, false);

    // printf("scan_key_state result: 0x%04X\n", state);
    return state;
}

void update_led(i2c_inst_t *i2c, int key, bool on) {
    uint8_t pin = LED_MAP[key];
    uint8_t addr;
    uint8_t *led_state;

    if (key < 8) {
        addr = PCF8574_LED_ADDR_1;
        led_state = &led_state_1;
    } else {
        addr = PCF8574_LED_ADDR_2;
        led_state = &led_state_2;
        key -= 8;
    }

    if (on) {
        *led_state &= ~(1 << pin); // Active LOW: 0 = ON
    } else {
        *led_state |= (1 << pin); // 1 = OFF
    }

    // Write updated state to PCF8574
    uint8_t data = *led_state;
    i2c_write_blocking(i2c, addr, &data, 1, false);
    // int result = i2c_write_blocking(i2c, addr, &data, 1, false);
    // if (result < 0) {
    //     // printf("I2C write FAILED to 0x%02X\n", addr);
    // } else {
    //     printf("Wrote 0x%02X to PCF8574 @ 0x%02X\n", data, addr);
    // }
}

void update_leds_from_keys(i2c_inst_t *i2c, uint16_t prev_state,
                           uint16_t curr_state) {
    uint16_t changed = prev_state ^ curr_state;

    for (int i = 0; i < 16; ++i) {
        if (changed & (1 << i)) {
            bool pressed = curr_state & (1 << i);
            printf("  Key %d %s\n", i, pressed ? "PRESSED" : "RELEASED");

            update_led(i2c, i, pressed);
        }
    }
}

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

    while (true) {

        // Handle USB tasks
        tud_task();

        // Handle MIDI messages
        midi_handler.midi_task();

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
                out2 = synth.get_output();
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
    std::array<int16_t, 1156> &out = (buff) ? out2 : out1;
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
