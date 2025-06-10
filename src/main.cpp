#include <cstdint>
#include <pico/types.h>
#include <stdio.h>

#include "pico/bootrom.h"

#include "config.hpp"
#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/pll.h"
#include "hardware/structs/clocks.h"

#include "pico/multicore.h"
#include "pico/util/queue.h"

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
#include "Sequencer.hpp"
#include "Synth.hpp"
#include "Ui.hpp"
#include "Wavetable.hpp"
// #include "i2s_init.hpp"
//
#include "audio.h"

static audio_buffer_pool *ap = nullptr;

static queue_t midi_queue;

Synth synth_core1 = Synth();

uint vol = 100;

// Double output buffer
std::array<int16_t, SAMPLES_PER_BUFFER> out_a;
std::array<int16_t, SAMPLES_PER_BUFFER> out_b;
std::array<int16_t, SAMPLES_PER_BUFFER> *front = &out_a;
std::array<int16_t, SAMPLES_PER_BUFFER> *back = &out_b;

bool need_render = 0;
bool buff = 0;

void decode();

void enter_bootsel_mode() {
    reset_usb_boot(0, 0); // Jump to BOOTSEL (UF2) mode
}

void setup_gpios(void) {
    // Enable less noise in audio output
    // gpio_init(PIN_DCDC_PSM_CTRL);
    // gpio_set_dir(PIN_DCDC_PSM_CTRL, GPIO_OUT);
    // gpio_put(PIN_DCDC_PSM_CTRL, 1);

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

std::array<int16_t, SAMPLES_PER_BUFFER> output;

void audio_task(void) {

    // clear accumulation buffer
    output.fill(0);

    synth_core1.out(output); // render into back buffer
    // This motherfucker needs to be blocking!
    audio_buffer_t *buffer = take_audio_buffer(ap, true);
    if (!buffer)
        return;

    int16_t *samples = (int16_t *)buffer->buffer->bytes;
    for (uint i = 0; i < buffer->max_sample_count; i++) {
        samples[i * 2 + 0] = output[i];
        samples[i * 2 + 1] = output[i];
    }

    buffer->sample_count = buffer->max_sample_count;
    give_audio_buffer(ap, buffer);
}

void audio_loop(void) {

    while (true) {
        uint8_t msg[4];
        while (queue_try_remove(&midi_queue, msg)) {
            synth_core1.process_midi_packet(msg);
            printf("Yo, on core 1, got a package\n");
        }
        audio_task();
    }
}

int main() {
    // Set up system clock for better audio
    pll_init(pll_usb, 1, 1536 * MHZ, 4, 4);
    clock_configure(clk_usb, 0, CLOCKS_CLK_USB_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB,
                    96 * MHZ, 48 * MHZ);
    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX,
                    CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, 150 * MHZ,
                    150 * MHZ);
    // Keep peripheral clock at standard rate for I2S timing
    clock_configure(clk_peri, 0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, 150 * MHZ,
                    150 * MHZ); // Standard 125MHz

    stdio_init_all();
    stdio_usb_init();

    // Initialize TinyUSB
    tusb_init();

    // Initialize I2S audio output
    ap = audio_init();

    setup_gpios();

    // const char *words[] = {"SSD1306", "DISPLAY", "DRIVER"};

    // ssd1306_t disp;
    //
    // disp.external_vcc = false;
    // ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
    //
    // // ssd1306_hflip(&disp, 1);
    // ssd1306_rotate(&disp, 1);
    // ssd1306_clear(&disp);

    Synth synth = Synth();

    MidiHandler midi_handler = MidiHandler(synth);

    // ssd1306_draw_string(&disp, 8, 24, 1, words[0]);
    // ssd1306_show(&disp);

    // uint16_t prev_state = 0;

    // static WaveType last_wave_type = static_cast<WaveType>(-1);
    HardwareManager hw = HardwareManager(synth);

    hw.init();

    printf("About to construct Sequencer\n");
    Sequencer seq(synth, midi_handler);
    printf("Constructed Sequencer\n");

    UiHandler ui = UiHandler(synth, hw, midi_handler, seq);

    queue_init(&midi_queue, 4, 64);
    multicore_launch_core1(audio_loop);

    while (true) {
        // Handle USB tasks
        tud_task();

        // Handle MIDI messages
        midi_handler.midi_task(midi_queue);

        hw.update();
        ui.update();
        seq.update();

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
            if (c == 'b')
                enter_bootsel_mode();
            printf("Yo\n\r");
        }

        // printf("In main loop\n");

        // if (need_render) {
        //     std::swap(front, back);     // swap front and back buffer
        //     synth.out();                // render into back buffer
        //     *back = synth.get_output(); // or synth.out(&back)
        //     need_render = 0;
        // }
        // decode();

        // else {
        //     // without this print the thing does not work,
        //     // main loop runs too fast atm, no time for interrupt
        //     // is what Im assuming
        //     // thats a good thing at least
        //     // printf("Not writing anything\n");
        //     // __wfe(); // Wait for event (low power waiting)
        // }
    }

    return 0;
}

// void decode() {
//     audio_buffer_t *buffer = take_audio_buffer(ap, false);
//     if (!buffer)
//         return;
//
//     // printf("In decode loop\n");
//
//     int16_t *samples = (int16_t *)buffer->buffer->bytes;
//     for (uint i = 0; i < buffer->max_sample_count; i++) {
//         int32_t v = (*front)[i];
//         samples[i * 2 + 0] = v + (v >> 16); // L
//         samples[i * 2 + 1] = v + (v >> 16); // R
//     }
//
//     buffer->sample_count = buffer->max_sample_count;
//     give_audio_buffer(ap, buffer);
//     need_render = 1; // Tell main loop to render next buffer
// }

// extern "C" {
// // callback from:
// //   void __isr __time_critical_func(audio_i2s_dma_irq_handler)()
// //   defined at my_pico_audio_i2s/audio_i2s.c
// //   where i2s_callback_func() is declared with __attribute__((weak))
// void i2s_callback_func() {
//     // if (decode_flg) {
//     decode();
//     // }
// }
// }
