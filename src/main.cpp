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

Synth synth = Synth();

std::array<int16_t, SAMPLES_PER_BUFFER> output;

// Sample player structure
typedef struct {
    FIL file;
    bool file_open;
    bool playing;
    uint8_t read_buffer[2048]; // 2KB read buffer
    size_t buffer_pos;
    size_t buffer_valid;
    bool eof_reached;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t data_start_pos;
} sample_player_t;

static sample_player_t g_sample_player;
static FATFS fs;
static queue_t sample_trigger_queue;

// WAV header parsing (simplified)
bool parse_wav_header(FIL *file, sample_player_t *player) {
    uint8_t header[44];
    UINT bytes_read;

    if (f_read(file, header, 44, &bytes_read) != FR_OK || bytes_read != 44) {
        return false;
    }

    // Check "RIFF" and "WAVE"
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        return false;
    }

    // Extract basic info (little-endian)
    player->channels = header[22] | (header[23] << 8);
    player->sample_rate = header[24] | (header[25] << 8) | (header[26] << 16) |
                          (header[27] << 24);
    player->bits_per_sample = header[34] | (header[35] << 8);

    // Find data chunk (simplified - assumes it's right after format chunk)
    player->data_start_pos = 44;

    return true;
}

// Initialize sample player
bool init_sample_player() {
    // Initialize SD card
    if (f_mount(&fs, "", 1) != FR_OK) {
        printf("Failed to mount SD card\n");
        return false;
    }

    // Open snare.wav
    if (f_open(&g_sample_player.file, "snare.wav", FA_READ) != FR_OK) {
        printf("Failed to open snare.wav\n");
        return false;
    }

    // Parse WAV header
    if (!parse_wav_header(&g_sample_player.file, &g_sample_player)) {
        printf("Failed to parse WAV header\n");
        f_close(&g_sample_player.file);
        return false;
    }

    printf("Loaded snare.wav: %ldHz, %d channels, %d bits\n",
           g_sample_player.sample_rate, g_sample_player.channels,
           g_sample_player.bits_per_sample);

    // Seek to data start
    f_lseek(&g_sample_player.file, g_sample_player.data_start_pos);

    g_sample_player.file_open = true;
    g_sample_player.playing = false;
    g_sample_player.buffer_pos = 0;
    g_sample_player.buffer_valid = 0;
    g_sample_player.eof_reached = false;

    return true;
}

// Trigger sample playback
void trigger_sample() {
    if (!g_sample_player.file_open)
        return;

    // Reset to beginning
    f_lseek(&g_sample_player.file, g_sample_player.data_start_pos);
    g_sample_player.playing = true;
    g_sample_player.buffer_pos = 0;
    g_sample_player.buffer_valid = 0;
    g_sample_player.eof_reached = false;

    // Send trigger to Core 1
    uint32_t trigger = 1;
    if (!queue_try_add(&sample_trigger_queue, &trigger)) {
        printf("Sample trigger queue full!\n");
    }
}

// Get next sample (called from Core 1)
int16_t get_next_sample() {
    if (!g_sample_player.playing || g_sample_player.eof_reached) {
        return 0;
    }

    // Refill buffer if needed
    if (g_sample_player.buffer_pos >= g_sample_player.buffer_valid) {
        UINT bytes_read;
        FRESULT result =
            f_read(&g_sample_player.file, g_sample_player.read_buffer,
                   sizeof(g_sample_player.read_buffer), &bytes_read);

        if (result != FR_OK || bytes_read == 0) {
            g_sample_player.eof_reached = true;
            g_sample_player.playing = false;
            return 0;
        }

        g_sample_player.buffer_valid = bytes_read;
        g_sample_player.buffer_pos = 0;
    }

    // Get sample (assuming 16-bit mono for now)
    int16_t sample = 0;
    if (g_sample_player.buffer_pos + 1 < g_sample_player.buffer_valid) {
        sample =
            (int16_t)(g_sample_player.read_buffer[g_sample_player.buffer_pos] |
                      (g_sample_player
                           .read_buffer[g_sample_player.buffer_pos + 1]
                       << 8));
        g_sample_player.buffer_pos += 2;
    }

    return sample;
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


// Modified audio_task function
void audio_task(void) {
    // Check for sample triggers
    uint32_t trigger;
    while (queue_try_remove(&sample_trigger_queue, &trigger)) {
        // Sample trigger received, already handled in trigger_sample()
        printf("Sample triggered on Core 1\n");
    }

    // Clear accumulation buffer
    output.fill(0);

    // Render synth
    synth.out(output);

    // Mix in sample
    for (uint i = 0; i < SAMPLES_PER_BUFFER; i++) {
        int16_t sample_value = get_next_sample();
        // Mix sample with synth (simple addition, you might want to scale)
        int32_t mixed = (int32_t)output[i] + (int32_t)sample_value;
        // Clamp to prevent overflow
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

    // Initialize sample player
    if (!init_sample_player()) {
        printf("Sample player initialization failed!\n");
        // Continue anyway for synth functionality
    }

    MidiHandler midi_handler = MidiHandler(midi_queue);

    HardwareManager hw = HardwareManager();

    hw.init();

    Sequencer seq(midi_handler);

    UiHandler ui = UiHandler(hw, midi_handler, seq);

    queue_init(&midi_queue, 4, 64);
    queue_init(&sample_trigger_queue, sizeof(uint32_t), 16); // Add this line
    multicore_launch_core1(audio_loop);

    // Timer for triggering sample every second
    absolute_time_t last_sample_time = get_absolute_time();

    while (true) {
        // Handle USB tasks
        tud_task();

        // Handle MIDI messages
        midi_handler.midi_task();

        hw.update();
        ui.update();
        seq.update();

        // Trigger sample every second
        if (absolute_time_diff_us(last_sample_time, get_absolute_time()) >=
            1000000) {
            trigger_sample();
            last_sample_time = get_absolute_time();
        }

        int c = getchar_timeout_us(0);
        if (c >= 0) {
            if (c == 'q')
                for (int i = 0; i < 512; i++) {
                    printf("%f,\n\r", q24_to_float(sinc_table_fp[i]));
                }
            if (c == 'b')
                enter_bootsel_mode();
            printf("Yo\n\r");
        }
    }

    return 0;
}
