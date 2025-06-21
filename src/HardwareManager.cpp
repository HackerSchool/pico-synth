#include "HardwareManager.hpp"
#include "Wavetable.hpp"
#include "fixed_point.h"
#include "ssd1306.h"
#include "ui/draw_utils.hpp"
#include <cstdint>
#include <cstdio>

uint8_t led_state_1 = 0xFF;
uint8_t led_state_2 = 0xFF;

const uint8_t COL_PINS[4] = {0, 1, 6, 4};
const uint8_t ROW_PINS[4] = {2, 3, 5, 7};

uint8_t LED_MAP[16] = {1, 3, 4, 7, 0, 2, 5, 6, 0, 2, 4, 6, 1, 3, 5, 7};

// Initialize a quadrature encoder PIO state machine
void init_encoder(Encoder *enc) {
    PIO pio = enc->pio;
    uint sm = enc->sm;
    uint pin_ab = enc->clk_pin;

    uint offset = pio_add_program(pio, &quadrature_encoder_program);
    quadrature_encoder_program_init(pio, sm, pin_ab, offset);

    // Optional: init button pin
    gpio_init(enc->sw_pin);
    gpio_set_dir(enc->sw_pin, GPIO_IN);
    gpio_pull_up(enc->sw_pin);
}

// // Get count from the encoder
// int32_t get_encoder_count(PIO pio, uint sm) {
//     return quadrature_encoder_get_count(pio, sm);
// }

uint16_t scan_key_state(i2c_inst_t *i2c) {
    uint16_t state = 0;

    for (int col = 0; col < 4; col++) {
        uint8_t data = 0xFF;
        data &= ~(1 << COL_PINS[col]); // Drive this column LOW

        // Send to PCF8574
        i2c_write_blocking(i2c, PCF8574_KEYPAD_ADDR, &data, 1, true);
        // sleep_us(5); // let signals settle

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
            // printf("  Key %d %s\n", i, pressed ? "PRESSED" : "RELEASED");

            update_led(i2c, i, pressed);
        }
    }
}

KeyChanges compute_key_changes(uint16_t prev_state, uint16_t curr_state) {
    KeyChanges changes = {0, 0};
    uint16_t changed = prev_state ^ curr_state;

    changes.note_on_mask = changed & curr_state;
    changes.note_off_mask = changed & prev_state;

    return changes;
}

HardwareManager::HardwareManager() {}

void HardwareManager::init() {

    for (int i = 0; i < NUM_ENCODERS; ++i) {
        init_encoder(&encoders[i]);
    }
    init_display();
}

void HardwareManager::init_display() {
    disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
    // ssd1306_hflip(&disp, 1);
    ssd1306_rotate(&disp, 1);
    ssd1306_clear(&disp);
    const char *words[] = {"PicoSynth", "v1.0"};
    ssd1306_draw_string(&disp, 8, 10, 2, words[0]);
    ssd1306_draw_string_inverted(&disp, 8, 30, 1, words[1]);
    ssd1306_show(&disp);
    sleep_ms(1000); // Hold the display for 1 second
}

void HardwareManager::update() {
    poll_inputs();
    // update_display();
}

void HardwareManager::poll_inputs() {
    poll_encoders();
    poll_keypad();
}

void HardwareManager::poll_encoders() {
    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &encoders[i];
        int32_t count = quadrature_encoder_get_count(enc->pio, enc->sm);
        int32_t delta = count - enc->last_count;
        enc->delta = delta;

        if (delta != 0 && abs(delta) > 1) {
            // here we only count delta bigger than 1 as a naive debouncer
            enc->last_count = count;
        }

        // Handle button press (edge detect)
        bool current_btn = gpio_get(enc->sw_pin);
        enc->button_edge = (current_btn != enc->button_state);
        enc->button_state = current_btn;
    }
}

void HardwareManager::poll_keypad() {
    curr_switches = scan_key_state(i2c0);
    // KeyChanges changes = compute_key_changes(prev_keys, curr);
    // update_leds_from_keys(i2c1, prev_keys, curr);
    //
    // for (int i = 0; i < 16; ++i) {
    //     if ((changes.note_on_mask >> i) & 1) {
    //         uint8_t note = key_to_midi[i];
    //         if (note != 255)
    //             synth.note_on(note, 127);
    //     }
    //     if ((changes.note_off_mask >> i) & 1) {
    //         uint8_t note = key_to_midi[i];
    //         if (note != 255)
    //             synth.note_off(note, 0);
    //     }
    // }
    //
    // prev_keys = curr;
}

void HardwareManager::update_leds(uint16_t prev, uint16_t curr) {
    update_leds_from_keys(i2c1, prev, curr); // use your existing helper
}

void HardwareManager::draw_notes() {
    ssd1306_clear_square(&disp, 8, 24, 120, 8);
    // ssd1306_draw_string(&disp, 8, 24, 1, synth.get_notes_playing_names());
}

void HardwareManager::draw_wave_type(uint8_t midi_channel, int8_t octave) {
    char buf[8]; // Enough for "16 Oct:+4\0"
    ssd1306_clear_square(&disp, 40, 0, 72, 8);

    ssd1306_draw_string(&disp, 8, 0, 1, "Chan:");
    sprintf(buf, "%d", midi_channel + 1); // Display channels as 1–16
    ssd1306_draw_string(&disp, 40, 0, 1, buf);

    // Calculate displayed octave
    int8_t display_octave = octave + 4;
    sprintf(buf, "Oc:%+d", display_octave);
    ssd1306_draw_string(&disp, 72, 0, 1, buf); // Draw after channel number

    // Draw waveform
    ssd1306_clear_square(&disp, 0, 8, 128, 24);
    draw_waveform(&disp, get_wavetable_for_channel(midi_channel), 0, 8, 128,
                  24);
}

void HardwareManager::draw_adsr(int current_adsr_param, uint8_t a, uint8_t d,
                                uint8_t s, uint8_t r) {
    ssd1306_clear_square(&disp, 0, 36, 128, 16); // 2 lines tall

    // Format values into strings with two digits (e.g., "85%")
    char values[4][8];
    snprintf(values[0], sizeof(values[0]), "%3d", a); // A
    snprintf(values[1], sizeof(values[1]), "%3d", d); // D
    snprintf(values[2], sizeof(values[2]), "%3d", s); // S
    snprintf(values[3], sizeof(values[3]), "%3d", r); // R

    // Draw parameter strings starting at x=8
    char line1[24], line2[24];
    snprintf(line1, sizeof(line1), "A:%s   D:%s", values[0], values[1]);
    snprintf(line2, sizeof(line2), "S:%s   R:%s", values[2], values[3]);
    ssd1306_draw_string(&disp, 8, 36, 1, line1);
    ssd1306_draw_string(&disp, 8, 44, 1, line2);

    // Draw arrow at one of four fixed positions based on current_adsr_param
    int arrow_x, arrow_y;
    switch (current_adsr_param) {
    case 0:
        arrow_x = 0;
        arrow_y = 36;
        break; // A
    case 1:
        arrow_x = 52;
        arrow_y = 36;
        break; // D
    case 2:
        arrow_x = 0;
        arrow_y = 44;
        break; // S
    case 3:
        arrow_x = 52;
        arrow_y = 44;
        break; // R
    default:
        arrow_x = 0;
        arrow_y = 36;
        break; // fallback
    }

    ssd1306_draw_char(&disp, arrow_x, arrow_y, 1, '>');
}

void HardwareManager::draw_filter(uint8_t filter_type, float cutoff) {
    // const char *filter_names[] = {"Off", "FIR", "Cheby"};
    char fc_value[32];

    // Display different information based on filter type
    switch (filter_type) {
    case 1:
        snprintf(fc_value, sizeof(fc_value), "FIR: %.1f Hz", cutoff);
        break;
    case 2:
        snprintf(fc_value, sizeof(fc_value), "Cheb: %.1f Hz", cutoff);
        break;
    default: // off
        snprintf(fc_value, sizeof(fc_value), "Filter: OFF");
        break;
    }

    ssd1306_clear_square(&disp, 0, 52, 128, 8); // Clear the entire line
    ssd1306_draw_string(&disp, 8, 52, 1, fc_value);
}

void HardwareManager::display_show() { ssd1306_show(&disp); }
void HardwareManager::display_clear() {
    ssd1306_clear_square(&disp, 0, 0, 128, 64);
}

void HardwareManager::draw_midi_settings(bool midi_out, bool midi_in,
                                         bool switches_in, bool sequencer_in,
                                         bool sequencer_out) {
    // Clear the display area for MIDI settings
    ssd1306_clear_square(&disp, 0, 0, 128, 64);

    // Title
    ssd1306_draw_string_inverted(&disp, 8, 0, 1, "MIDI Settings");

    // Draw each setting with ON/OFF status
    char line1[20], line2[20], line3[20], line4[20], line5[20];

    snprintf(line1, sizeof(line1), "MIDI Out: %s", midi_out ? "ON" : "OFF");
    snprintf(line2, sizeof(line2), "MIDI In:  %s", midi_in ? "ON" : "OFF");
    snprintf(line3, sizeof(line3), "Switches: %s", switches_in ? "ON" : "OFF");
    snprintf(line4, sizeof(line4), "Seq In:   %s", sequencer_in ? "ON" : "OFF");
    snprintf(line5, sizeof(line5), "Seq Out:  %s",
             sequencer_out ? "ON" : "OFF");

    ssd1306_draw_string(&disp, 8, 12, 1, line1);
    ssd1306_draw_string(&disp, 8, 20, 1, line2);
    ssd1306_draw_string(&disp, 8, 28, 1, line3);
    ssd1306_draw_string(&disp, 8, 36, 1, line4);
    ssd1306_draw_string(&disp, 8, 44, 1, line5);

    // Instructions at bottom
    ssd1306_draw_string(&disp, 8, 56, 1, "Btn4: Back");
}

void HardwareManager::draw_choose_menu(int chosen_index) {
    // Clear the display area for MIDI settings
    ssd1306_clear_square(&disp, 0, 0, 128, 64);

    // Title
    ssd1306_draw_string_inverted(&disp, 8, 0, 1, "Select Menu");

    // Draw each setting with ON/OFF status
    char line1[20], line2[20], line3[20], line4[20], line5[20];

    snprintf(line1, sizeof(line1), "Synth %s", chosen_index == 0 ? "<" : "");
    snprintf(line2, sizeof(line2), "FM Edit %s", chosen_index == 1 ? "<" : "");
    snprintf(line3, sizeof(line3), "MIDI %s", chosen_index == 2 ? "<" : "");
    snprintf(line4, sizeof(line4), "Sequencer %s" , chosen_index == 3 ? "<" : "");
    snprintf(line5, sizeof(line5), "Sampler %s" , chosen_index == 4 ? "<" : "");
    //snprintf(line5, sizeof(line5), "%d" , chosen_index);

    ssd1306_draw_string(&disp, 8, 12, 1, line1);
    ssd1306_draw_string(&disp, 8, 20, 1, line2);
    ssd1306_draw_string(&disp, 8, 28, 1, line3);
    ssd1306_draw_string(&disp, 8, 36, 1, line4);
    ssd1306_draw_string(&disp, 8, 44, 1, line5);
}

void HardwareManager::draw_sampler_menu(const WavFileList& wav_files, int sample_index, int sample_channel) {
    // Clear the display area for MIDI settings
    ssd1306_clear_square(&disp, 0, 0, 128, 64);

    // Title
    ssd1306_draw_string_inverted(&disp, 8, 0, 1, "Sampler Settings");

    // Draw each setting with ON/OFF status
    char line1[20], line2[20], line3[20], line4[20];

    snprintf(line1, sizeof(line1), "Channel: %d", sample_channel);
    snprintf(line2, sizeof(line2), "Switch: ");
    
    // Get sample name from WAV file list
    if (wav_files.get_count() > 0 && sample_index < wav_files.get_count()) {
        snprintf(line3, sizeof(line3), "Sample: %s", wav_files.get_filename(sample_index).c_str());
    } else {
        snprintf(line3, sizeof(line3), "Sample: No files");
    }

    //ssd1306_draw_string(&disp, 8, 12, 1, line1);
    ssd1306_draw_string(&disp, 8, 20, 1, line1);
    ssd1306_draw_string(&disp, 8, 28, 1, line2);
    ssd1306_draw_string(&disp, 8, 36, 1, line3);
    ssd1306_draw_string(&disp, 8, 44, 1, line4);

}

// Implementation in HardwareManager
void HardwareManager::draw_sequencer_settings(bool playing, uint32_t tempo,
                                              uint8_t current_step) {
    // Clear the display area
    ssd1306_clear_square(&disp, 0, 0, 128, 64);

    // Title
    ssd1306_draw_string_inverted(&disp, 8, 0, 1, "Sequencer");

    // Play/Pause status
    char status_line[20];
    snprintf(status_line, sizeof(status_line), "Status: %s",
             playing ? "PLAYING" : "PAUSED");
    ssd1306_draw_string(&disp, 8, 12, 1, status_line);

    // Tempo display
    char tempo_line[20];
    snprintf(tempo_line, sizeof(tempo_line), "Tempo: %ld BPM", tempo);
    ssd1306_draw_string(&disp, 8, 20, 1, tempo_line);

    // Current step display
    char step_line[20];
    snprintf(step_line, sizeof(step_line), "Step: %d/16", current_step + 1);
    ssd1306_draw_string(&disp, 8, 28, 1, step_line);

    // Visual step indicator (dots or bars)
    const int step_width = 6;
    const int step_spacing = 7;
    const int start_x = 8;
    const int start_y = 38;

    for (int i = 0; i < 16; i++) {
        int x = start_x + (i * step_spacing);
        if (i == current_step) {
            // Current step - filled rectangle
            ssd1306_draw_square(&disp, x, start_y, step_width, 6);
        } else {
            // Other steps - empty rectangle
            ssd1306_draw_line(&disp, x, start_y, x + step_width, start_y);
            ssd1306_draw_line(&disp, x, start_y + 6, x + step_width,
                              start_y + 6);
            ssd1306_draw_line(&disp, x, start_y, x, start_y + 6);
            ssd1306_draw_line(&disp, x + step_width, start_y, x + step_width,
                              start_y + 6);
        }
    }

    // Instructions at bottom
    ssd1306_draw_string(&disp, 8, 48, 1, "E1: Tempo E1btn: Play");
    ssd1306_draw_string(&disp, 8, 56, 1, "E2btn: Reset E4: Back");
}

// Implementation in HardwareManager
void HardwareManager::draw_sequencer_note_edit(
    uint8_t current_step, uint8_t midi_channel, bool auto_stepping_enabled,
    bool is_playing, uint8_t *step_notes, uint8_t *step_channels) {
    // Clear the display area
    // printf("In the draw function for note edit\n");
    ssd1306_clear_square(&disp, 0, 0, 128, 64);

    // Title
    ssd1306_draw_string_inverted(&disp, 8, 0, 1, "Note Edit");

    // Current step and channel info
    char step_line[20];
    snprintf(step_line, sizeof(step_line), "Step:%d/16 Ch:%d", current_step + 1,
             midi_channel + 1);
    ssd1306_draw_string(&disp, 8, 10, 1, step_line);

    // Play/pause and auto-step status
    char status_line[20];
    snprintf(status_line, sizeof(status_line), "%s %s",
             is_playing ? "PLAY" : "STOP",
             auto_stepping_enabled ? "AUTO" : "MAN");
    ssd1306_draw_string(&disp, 8, 18, 1, status_line);

    // Visual step indicator (16 steps in a row)
    const int step_width = 6;
    const int step_spacing = 7;
    const int start_x = 8;
    const int start_y = 26;

    for (int i = 0; i < 16; i++) {
        int x = start_x + (i * step_spacing);
        if (i == current_step) {
            // Current step - filled rectangle
            ssd1306_draw_square(&disp, x, start_y, step_width, 4);
        } else {
            // Other steps - check if they have notes
            bool has_notes = false;
            // You might want to check all steps for notes, but for now just
            // show empty
            if (has_notes) {
                // Step with notes - draw outline
                ssd1306_draw_line(&disp, x, start_y, x + step_width, start_y);
                ssd1306_draw_line(&disp, x, start_y + 4, x + step_width,
                                  start_y + 4);
                ssd1306_draw_line(&disp, x, start_y, x, start_y + 4);
                ssd1306_draw_line(&disp, x + step_width, start_y,
                                  x + step_width, start_y + 4);
            } else {
                // Empty step - just a dot
                ssd1306_draw_pixel(&disp, x + 2, start_y + 2);
            }
        }
    }

    bool use_line2 = false;
    // Show notes on current step
    ssd1306_draw_string(&disp, 8, 34, 1, "Notes on step:");

    if (step_notes != nullptr && step_channels != nullptr) {
        char notes_line1[21] = {0}; // For first line of notes
        char notes_line2[21] = {0}; // For second line if needed
        int line1_pos = 0;
        int line2_pos = 0;

        const int NOTES_PER_STEP = 8;
        const int UNASSIGNED = 200;

        for (int i = 0; i < NOTES_PER_STEP; i++) {
            if (step_notes[i] != UNASSIGNED) {
                // Convert MIDI note to note name
                const char *note_names[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                            "F#", "G",  "G#", "A",  "A#", "B"};
                uint8_t note_num = step_notes[i] % 12;
                // uint8_t octave = step_notes[i] / 12 - 1;
                uint8_t channel = step_channels[i];

                char note_str[8];
                snprintf(note_str, sizeof(note_str), "%s(%d) ",
                         note_names[note_num], channel + 1);

                // Try to fit in first line, otherwise use second line
                if (line1_pos + strlen(note_str) < 20 && !use_line2) {
                    strcat(notes_line1, note_str);
                    line1_pos += strlen(note_str);
                } else {
                    use_line2 = true;
                    if (line2_pos + strlen(note_str) < 20) {
                        strcat(notes_line2, note_str);
                        line2_pos += strlen(note_str);
                    }
                }
            }
        }

        // Draw notes (or "None" if empty)
        if (line1_pos == 0) {
            ssd1306_draw_string(&disp, 8, 42, 1, "None");
        } else {
            ssd1306_draw_string(&disp, 8, 42, 1, notes_line1);
            if (use_line2 && line2_pos > 0) {
                ssd1306_draw_string(&disp, 8, 50, 1, notes_line2);
            }
        }
    }

    // Instructions at bottom
    if (!use_line2) {
        ssd1306_draw_string(&disp, 8, 50, 1, "Keys: Note 3+0/11: Step");
        ssd1306_draw_string(&disp, 8, 58, 1, "E1: Play E2: Auto E4: Back");
    } else {
        ssd1306_draw_string(&disp, 8, 58, 1, "E1:Play E2:Auto E4:Back");
    }
}

void HardwareManager::draw_fm_edit(uint8_t midi_channel,
                                   uint8_t selected_operator, int8_t octave,
                                   uint8_t fm_edit_mode, WaveType wave_type,
                                   uint8_t attack, uint8_t decay,
                                   uint8_t sustain, uint8_t release,
                                   uint16_t ratio, uint16_t feedback,
                                   uint16_t fm_depth) {
    // Clear the display area
    ssd1306_clear_square(&disp, 0, 0, 128, 64);

    // Header with channel/operator info integrated
    char header[25];
    snprintf(header, sizeof(header), "FM Ch%d Op%d", midi_channel + 1,
             selected_operator + 1);
    ssd1306_draw_string_inverted(&disp, 4, 0, 1, header);

    // Octave indicator (top right)
    char oct_str[8];
    snprintf(oct_str, sizeof(oct_str), "Oct%+d", octave);
    ssd1306_draw_string(&disp, 88, 2, 1, oct_str);

    // Mode tabs with visual separation
    const char *mode_names[] = {"SEL", "ADSR", "PARAM"};
    for (int i = 0; i < 3; i++) {
        int x_pos = 8 + (i * 38);
        if (i == fm_edit_mode) {
            // Active tab - inverted
            ssd1306_draw_string_inverted(&disp, x_pos, 12, 1, mode_names[i]);
        } else {
            // Inactive tab - normal
            ssd1306_draw_string(&disp, x_pos, 12, 1, mode_names[i]);
        }
    }

    // Separator line
    for (int x = 4; x < 124; x++) {
        ssd1306_draw_pixel(&disp, x, 22);
    }

    // Content area based on mode
    switch (fm_edit_mode) {
    case 0: // Operator selection
    {
        // Wave type with icon/symbol
        const char *wave_symbols[] = {"~", "/|", "▯", "△"};
        const char *wave_names[] = {"Sine", "Saw", "Square", "Triangle"};

        char wave_line[20];
        snprintf(wave_line, sizeof(wave_line), "%s %s",
                 wave_symbols[static_cast<int>(wave_type)],
                 wave_names[static_cast<int>(wave_type)]);
        ssd1306_draw_string(&disp, 8, 28, 1, wave_line);

        // Parameters in a clean grid
        char ratio_str[12];
        snprintf(ratio_str, sizeof(ratio_str), "Ratio %d", ratio);
        ssd1306_draw_string(&disp, 8, 38, 1, ratio_str);

        char fb_str[12];
        snprintf(fb_str, sizeof(fb_str), "FB %d", feedback);
        ssd1306_draw_string(&disp, 70, 38, 1, fb_str);

        char depth_str[15];
        snprintf(depth_str, sizeof(depth_str), "Depth %d", fm_depth);
        ssd1306_draw_string(&disp, 8, 48, 1, depth_str);
    } break;

    case 1: // ADSR mode
    {
        // ADSR with visual bars/indicators
        char attack_str[12];
        snprintf(attack_str, sizeof(attack_str), "A %3d", attack);
        ssd1306_draw_string(&disp, 8, 28, 1, attack_str);

        char decay_str[12];
        snprintf(decay_str, sizeof(decay_str), "D %3d", decay);
        ssd1306_draw_string(&disp, 70, 28, 1, decay_str);

        char sustain_str[12];
        snprintf(sustain_str, sizeof(sustain_str), "S %3d", sustain);
        ssd1306_draw_string(&disp, 8, 40, 1, sustain_str);

        char release_str[12];
        snprintf(release_str, sizeof(release_str), "R %3d", release);
        ssd1306_draw_string(&disp, 70, 40, 1, release_str);

        // Dynamic ADSR envelope visualization
        const int env_start_x = 4;
        const int env_end_x = 124;
        const int env_top_y = 30;
        const int env_bottom_y = 60;
        const int env_height = env_bottom_y - env_top_y;

        // Calculate segment widths using bitshifts (fast division)
        int total_width = env_end_x - env_start_x;
        int attack_width = (attack * total_width >> 2) >>
                           7; // /4 then /128 (bitshift approximation of /127)
        int decay_width = (decay * total_width >> 2) >> 7; // /4 then /128
        int sustain_width =
            total_width / 3; // Keep division for 1/3 (no clean bitshift)
        int release_width = (release * total_width >> 2) >> 7; // /4 then /128

        // Allow zero widths for instant attack/decay/release (vertical lines)
        // No minimum width constraints

        // Calculate sustain level using bitshift (higher sustain value = higher
        // on screen)
        int sustain_y = env_bottom_y - (sustain * env_height >>
                                        7); // /128 approximation of /127

        // Draw the envelope
        int x = env_start_x;

        ssd1306_draw_line(&disp, x, env_bottom_y, x + attack_width, env_top_y);
        x += attack_width;

        ssd1306_draw_line(&disp, x, env_top_y, x + decay_width, sustain_y);
        x += decay_width;

        // Sustain: hold at sustain level
        ssd1306_draw_line(&disp, x, sustain_y, x + sustain_width, sustain_y);
        x += sustain_width;

        ssd1306_draw_line(&disp, x, sustain_y, x + release_width, env_bottom_y);

        // Draw baseline
        ssd1306_draw_line(&disp, env_start_x, env_bottom_y, env_end_x,
                          env_bottom_y);
    } break;

    case 2: // Parameter mode
    {
        // Clean parameter layout
        const char *wave_short[] = {"Sin", "Saw", "Sqr", "Tri"};

        char line1[20];
        snprintf(line1, sizeof(line1), "Ratio  %d", ratio);
        ssd1306_draw_string(&disp, 8, 28, 1, line1);

        char line2[20];
        snprintf(line2, sizeof(line2), "FB     %d", feedback);
        ssd1306_draw_string(&disp, 8, 36, 1, line2);

        char line3[20];
        snprintf(line3, sizeof(line3), "Depth  %d", fm_depth);
        ssd1306_draw_string(&disp, 8, 44, 1, line3);

        char line4[20];
        snprintf(line4, sizeof(line4), "Wave   %s",
                 wave_short[static_cast<int>(wave_type)]);
        ssd1306_draw_string(&disp, 8, 52, 1, line4);
    } break;
    }
}
