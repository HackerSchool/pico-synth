#include <array>
#include <cstdint>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/pll.h"
#include "hardware/structs/clocks.h"

#include "pico/audio.h"

#include "tusb.h"
// #include "tusb_config.h"

#include "quadrature_encoder.pio.h"

#include "Envelope.cpp"
#include "Oscillator.cpp"
#include "Wavetable.hpp"
#include "i2s_init.hpp"

uint vol = 100;

// Double output buffer
std::array<int16_t, 1156> out1 = {};
std::array<int16_t, 1156> out2 = {};
bool write_flag = 0;
bool buff = 0;

#define MIDI_MIN 0
#define MIDI_MAX 127

static const float midi_frequencies[MIDI_MAX + 1] = {
    8.1758f,    8.6610f,    9.1770f,   9.7227f,   10.3009f,   10.9134f,
    11.5623f,   12.2499f,   12.9783f,  13.7500f,  14.5676f,   15.4339f,
    16.3516f,   17.3239f,   18.3540f,  19.4454f,  20.6017f,   21.8268f,
    23.1247f,   24.4997f,   25.9565f,  27.5000f,  29.1352f,   30.8677f,
    32.7032f,   34.6478f,   36.7081f,  38.8909f,  41.2034f,   43.6535f,
    46.2493f,   48.9994f,   51.9131f,  55.0000f,  58.2705f,   61.7354f,
    65.4064f,   69.2957f,   73.4162f,  77.7817f,  82.4069f,   87.3071f,
    92.4986f,   97.9989f,   103.826f,  110.000f,  116.541f,   123.471f,
    130.813f,   138.591f,   146.832f,  155.563f,  164.814f,   174.614f,
    184.997f,   195.998f,   207.652f,  220.000f,  233.082f,   246.942f,
    261.626f,   277.183f,   293.665f,  311.127f,  329.628f,   349.228f,
    369.994f,   391.995f,   415.305f,  440.000f,  466.164f,   493.883f,
    523.251f,   554.365f,   587.330f,  622.254f,  659.255f,   698.456f,
    739.989f,   783.991f,   830.609f,  880.000f,  932.328f,   987.767f,
    1046.50f,   1108.73f,   1174.66f,  1244.51f,  1318.51f,   1396.91f,
    1479.98f,   1567.98f,   1661.22f,  1760.00f,  1864.66f,   1975.53f,
    2093.00f,   2217.46f,   2349.32f,  2489.02f,  2637.02f,   2793.83f,
    2959.96f,   3135.96f,   3322.44f,  3520.00f,  3729.31f,   3951.07f,
    4186.01f,   4434.92f,   4698.63f,  4978.03f,  5274.04f,   5587.65f,
    5919.91f,   6271.93f,   6644.88f,  7040.00f,  7458.62f,   7902.13f,
    8372.018f,  8869.844f,  9397.273f, 9956.063f, 10548.080f, 11175.300f,
    11839.820f, 12543.850f,
};


Oscillator osc1 = Oscillator(Square, 440);
ADSREnvelope env1 = ADSREnvelope(0.5f, 0.1f, 0.4f, 1.f, osc1.get_output(), 5.f);

float midi_to_freq(uint8_t midi_note) {
    if (midi_note > MIDI_MAX)
        return 0.0f;
    return midi_frequencies[midi_note];
}

void process_midi_packet(uint8_t packet[4]) {
    uint8_t msg_type = packet[1] & 0xF0;
    uint8_t channel = packet[1] & 0x0F;
    uint8_t note = packet[2];
    uint8_t velocity = packet[3];

    switch (msg_type) {
    case 0x90: // Note On
        if (velocity > 0) {
            // Note on with velocity
            printf("Note On: channel=%d, note=%d, velocity=%d\n", channel, note,
                   velocity);
            // Call your synth's note on function here
            // For example: play_note(note, velocity);
            float freq = midi_to_freq(note);
            osc1.set_freq(freq);
            env1.set_trigger(5.f);
        } else {
            // Note on with velocity 0 is equivalent to Note Off
            printf("Note Off (via Note On): channel=%d, note=%d\n", channel,
                   note);
            // Call your synth's note off function here
            // For example: stop_note(note);
        }
        break;

    case 0x80: // Note Off
        printf("Note Off: channel=%d, note=%d, velocity=%d\n", channel, note,
               velocity);
        // Call your synth's note off function here
        // For example: stop_note(note);
        env1.set_trigger(0.f);
        break;

    case 0xB0: // Control Change
        printf("Control Change: channel=%d, controller=%d, value=%d\n", channel,
               note, velocity);
        // Handle control change message
        // For example: set_controller(note, velocity);
        break;

        // Add other MIDI message types as needed
    }
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

//--------------------------------------------------------------------+
// MIDI Task
//--------------------------------------------------------------------+

// Variable that holds the current position in the sequence.
uint32_t note_pos = 0;

// Store example melody as an array of note values
const uint8_t note_sequence[] = {
    74, 78, 81, 86,  90, 93, 98, 102, 57, 61,  66, 69, 73, 78, 81, 85,
    88, 92, 97, 100, 97, 92, 88, 85,  81, 78,  74, 69, 66, 62, 57, 62,
    66, 69, 74, 78,  81, 86, 90, 93,  97, 102, 97, 93, 90, 85, 81, 78,
    73, 68, 64, 61,  56, 61, 64, 68,  74, 78,  81, 86, 90, 93, 98, 102};

void midi_task(void) {

    uint8_t packet[4];

    // Check if there are any MIDI messages
    while (tud_midi_available()) {
        printf("midi available\n\r");
        tud_midi_packet_read(packet);
        process_midi_packet(packet);
    }
    static uint32_t start_ms = 0;

    uint8_t const cable_num = 0; // MIDI jack associated with USB endpoint
    uint8_t const channel = 0;   // 0 for channel 1

    // The MIDI interface always creates input and output port/jack descriptors
    // regardless of these being used or not. Therefore incoming traffic should
    // be read (possibly just discarded) to avoid the sender blocking in IO
    while (tud_midi_available()) {
        uint8_t packet[4];
        tud_midi_packet_read(packet);
    }

    // send note periodically
    uint32_t current_ms = to_ms_since_boot(get_absolute_time());
    if (current_ms - start_ms < 286) {
        return; // not enough time
    }
    start_ms += 286;

    // Previous positions in the note sequence.
    int previous = (int)(note_pos - 1);

    // If we currently are at position 0, set the
    // previous position to the last note in the sequence.
    if (previous < 0) {
        previous = sizeof(note_sequence) - 1;
    }

    // Send Note On for current position at full velocity (127) on channel 1.
    uint8_t note_on[3] = {0x90 | channel, note_sequence[note_pos], 127};
    tud_midi_stream_write(cable_num, note_on, 3);

    // Send Note Off for previous note.
    uint8_t note_off[3] = {0x80 | channel, note_sequence[previous], 0};
    tud_midi_stream_write(cable_num, note_off, 3);

    // Increment position
    note_pos++;

    // If we are at the end of the sequence, start over.
    if (note_pos >= sizeof(note_sequence)) {
        note_pos = 0;
    }
}


int main() {
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
    stdio_usb_init();

    // Initialize TinyUSB
    tusb_init();

    // Enable less noise in audio output
    gpio_init(PIN_DCDC_PSM_CTRL);
    gpio_set_dir(PIN_DCDC_PSM_CTRL, GPIO_OUT);
    gpio_put(PIN_DCDC_PSM_CTRL, 1);

    // Initialize I2S audio output
    ap = i2s_audio_init(44100);

    while (true) {

        // Handle USB tasks
        tud_task();

        // Handle MIDI messages
        midi_task();

        int c = getchar_timeout_us(0);
        if (c >= 0) {
            if (c == '-' && vol)
                vol--;
            if ((c == '=' || c == '+') && vol < 256)
                vol++;
            if (c == 's')
                env1.set_trigger(5.0);
            if (c == 'p')
                env1.set_trigger(0.0);
            if (c == 'q')
                break;
            printf("Yo\n\r");
        }
        if (write_flag) {
            if (buff) {
                osc1.out();
                env1.out();
                out1 = env1.get_output();
                write_flag = 0;
            } else {
                osc1.out();
                env1.out();
                out2 = env1.get_output();
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
