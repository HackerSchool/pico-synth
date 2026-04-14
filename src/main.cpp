#include <cstdint>
#include <pico/types.h>
#include <stdio.h>
#include <pico/stdlib.h>
#include <pico/time.h>
#include <stdlib.h>
#include <unistd.h>

#include "pico/audio.h"
#include "pico/bootrom.h"
#include "pico/multicore.h"
#include "pico/util/queue.h"

#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/clocks.h"

#include "fixed_point.h"
#include "tusb.h"
#include <bsp/board.h>

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

#include <cstring>
#include <limits>

static audio_buffer_pool *ap = nullptr;

static queue_t midi_queue;
// static queue_t sample_trigger_queue;

Synth synth = Synth();
Sampler sampler;

std::array<int16_t, SAMPLES_PER_BUFFER> output;
std::array<int16_t, SAMPLES_PER_BUFFER> sampler_buffer;

static volatile uintptr_t core0_min_sp = std::numeric_limits<uintptr_t>::max();
static volatile uintptr_t core1_min_sp = std::numeric_limits<uintptr_t>::max();

static inline uintptr_t current_stack_pointer() {
    volatile uint8_t stack_probe = 0;
    return reinterpret_cast<uintptr_t>(&stack_probe);
}

static inline void sample_stack_usage_core0() {
    const uintptr_t sp = current_stack_pointer();
    if (sp < core0_min_sp) {
        core0_min_sp = sp;
    }
}

static inline void sample_stack_usage_core1() {
    const uintptr_t sp = current_stack_pointer();
    if (sp < core1_min_sp) {
        core1_min_sp = sp;
    }
}

static void print_ram_usage_percent() {
    uintptr_t min_sp = core0_min_sp;
    if (core1_min_sp < min_sp) {
        min_sp = core1_min_sp;
    }
    if (min_sp == std::numeric_limits<uintptr_t>::max()) {
        min_sp = current_stack_pointer();
    }

    const uintptr_t ram_base = static_cast<uintptr_t>(SRAM_BASE);
    const uintptr_t ram_end = static_cast<uintptr_t>(SRAM_END);
    if (min_sp < ram_base) min_sp = ram_base;
    if (min_sp > ram_end) min_sp = ram_end;

    // Low-memory usage grows upward (.data/.bss/heap), stack grows downward.
    uintptr_t heap_top = reinterpret_cast<uintptr_t>(sbrk(0));
    if (heap_top == static_cast<uintptr_t>(-1)) {
        heap_top = ram_base;
    }
    if (heap_top < ram_base) heap_top = ram_base;
    if (heap_top > ram_end) heap_top = ram_end;

    const uint32_t ram_total = static_cast<uint32_t>(ram_end - ram_base);
    uint32_t ram_used = static_cast<uint32_t>((heap_top - ram_base) +
                                              (ram_end - min_sp));
    if (ram_used > ram_total) ram_used = ram_total;
    const float ram_percent =
        ram_total == 0 ? 0.0f
                       : (100.0f * static_cast<float>(ram_used) /
                          static_cast<float>(ram_total));

    printf("RAM: %.1f%% (%lu/%lu bytes)\n", static_cast<double>(ram_percent),
           static_cast<unsigned long>(ram_used),
           static_cast<unsigned long>(ram_total));
}

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

    // Mix sampler with synth, then run the shared FX chain on the full signal.
    // Use 64-bit accumulation to prevent overflow from multiple voices
    for (uint i = 0; i < SAMPLES_PER_BUFFER; i++) {
        int64_t mixed = (int64_t)output[i] + (int64_t)(sampler_buffer[i] >> 2);
        if (mixed > 32767)
            mixed = 32767;
        if (mixed < -32768)
            mixed = -32768;
        output[i] = (int16_t)mixed;
    }

    synth.process_fx(output);

    // Send to audio buffer (your existing code)
    audio_buffer_t *buffer = take_audio_buffer(ap, true);
    if (!buffer)
        return;

    // Ensure buffer size matches our audio generation size to prevent overflow
    if (buffer->max_sample_count != SAMPLES_PER_BUFFER) {
        panic("Audio buffer size mismatch: expected %lu, got %lu",
              (unsigned long)SAMPLES_PER_BUFFER,
              (unsigned long)buffer->max_sample_count);
    }

    uint8_t *sample_bytes = buffer->buffer->bytes;
    for (uint i = 0; i < buffer->max_sample_count; i++) {
        const int16_t left = output[i];
        const int16_t right = output[i];
        std::memcpy(sample_bytes + (i * 4), &left, sizeof(left));
        std::memcpy(sample_bytes + (i * 4) + sizeof(left), &right, sizeof(right));
    }
    buffer->sample_count = buffer->max_sample_count;
    give_audio_buffer(ap, buffer);
}

void audio_loop(void) {

    while (true) {
        sample_stack_usage_core1();
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
    if (!ap) {
        panic("audio_init() failed - unable to initialize audio buffer pool");
    }

    setup_gpios();

    if (!sampler.init()) {
        printf("Sampler initialization failed!\n");
    }

    sampler.load_sample(0, "kick.wav");
    sampler.load_sample(1, "snare.wav");
    sampler.load_sample(2, "closehat.wav");
    sampler.load_sample(3, "crash.wav");
    sampler.load_sample(4, "kick.wav");

    static MidiHandler midi_handler(midi_queue);

    static HardwareManager hw;

    hw.init();

    static Sequencer seq(midi_handler, sampler);

    static UiHandler ui(hw, midi_handler, seq, sampler, synth);

    queue_init(&midi_queue, 4, 64);
    // queue_init(&sample_trigger_queue, sizeof(uint32_t), 16); // Add this line
    multicore_launch_core1(audio_loop);
    uint32_t last_ram_print_ms = to_ms_since_boot(get_absolute_time());

    while (true) {
        sample_stack_usage_core0();
        // Handle USB tasks
        tud_task();

        // Handle MIDI messages
        midi_handler.midi_task();

        hw.update();
        ui.update();
        seq.update();

        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - last_ram_print_ms >= 2000) {
            last_ram_print_ms = now_ms;
            print_ram_usage_percent();
        }

        int c = getchar_timeout_us(0);
        if (c >= 0) {
            if (c == 'q')
                for (int i = 0; i < 512; i++) {
                    printf("%f,\n\r", static_cast<double>(q24_to_float(sinc_table_fp[i])));
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
            if (c == '5')
                sampler.trigger_player(4); // Trigger kick
        }
    }

    return 0;
}
