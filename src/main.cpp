#include <cstdint>
#include <pico/types.h>
#include <stdio.h>

#include "pico/audio.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"

#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/structs/clocks.h"

#include "fixed_point.h"
#include "tusb.h"

#include "HardwareManager.hpp"
#include "MidiHandler.hpp"
#include "Sampler.hpp"
#include "Sequencer.hpp"
#include "Synth.hpp"
#include "Ui.hpp"
#include "Wavetable.hpp"
#include "audio.h"
#include "config.hpp"

#include "diskio.h"    // FatFS disk I/O
#include "ff.h"        // FatFS
#include "hw_config.h" // SD card hardware config

static audio_buffer_pool *ap = nullptr;

static queue_t midi_queue;
// static queue_t sample_trigger_queue;

Synth synth = Synth();
Sampler sampler;

std::array<int16_t, SAMPLES_PER_BUFFER> output;
std::array<int16_t, SAMPLES_PER_BUFFER> sampler_buffer;

void enter_bootsel_mode() {
    reset_usb_boot(0, 0); // Jump to BOOTSEL (UF2) mode
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

void audio_task(void) {
    // Clear accumulation buffer
    output.fill(0);

    // Render synth
    synth.out(output);

    sampler.out(sampler_buffer);

    // Mix sampler with synth
    for (uint i = 0; i < SAMPLES_PER_BUFFER; i++) {
        int32_t mixed = (int32_t)output[i] + (int32_t)sampler_buffer[i];
        if (mixed > 32767)
            mixed = 32767;
        if (mixed < -32768)
            mixed = -32768;
        output[i] = (int16_t)mixed;
    }

    // Send to audio buffer (your existing code)
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
            synth.process_midi_packet(msg);
            // printf("Yo, on core 1, got a package\n");
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
    clock_configure(clk_peri, 0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS, 150 * MHZ,
                    150 * MHZ);

    // Initialize TinyUSB
    tusb_init();
    stdio_usb_init();

    // Initialize I2S audio output
    ap = audio_init();

    setup_gpios();

    if (!sampler.init()) {
        printf("Sampler initialization failed!\n");
    }

    sampler.load_sample(0, "kick.wav");
    sampler.load_sample(1, "snare.wav");
    sampler.load_sample(2, "closehat.wav");
    sampler.load_sample(3, "crash.wav");

    MidiHandler midi_handler = MidiHandler(midi_queue);

    HardwareManager hw = HardwareManager();

    hw.init();

    Sequencer seq(midi_handler, sampler);

    UiHandler ui = UiHandler(hw, midi_handler, seq, sampler);

    queue_init(&midi_queue, 4, 64);
    // queue_init(&sample_trigger_queue, sizeof(uint32_t), 16); // Add this line
    multicore_launch_core1(audio_loop);

    while (true) {
        // Handle USB tasks
        tud_task();

        // Handle MIDI messages
        midi_handler.midi_task();

        hw.update();
        ui.update();
        seq.update();

        int c = getchar_timeout_us(0);
        if (c >= 0) {
            if (c == 'q')
                for (int i = 0; i < 512; i++) {
                    printf("%f,\n\r", q24_to_float(sinc_table_fp[i]));
                }
            if (c == 'b')
                enter_bootsel_mode();
            // Trigger samples
            if (c == '1')
                sampler.trigger_player(0); // Trigger kick
            if (c == '2')
                sampler.trigger_player(1); // Trigger snare
            if (c == '3')
                sampler.trigger_player(2); // Trigger hihat
            if (c == '4')
                sampler.trigger_player(3); // Trigger crash
            printf("Yo\n\r");
        }
    }

    return 0;
}
