#include "HardwareManager.hpp"
#include "fixed_point.h"
#include "ssd1306.h"
#include <cstdio>

uint8_t led_state_1 = 0xFF;
uint8_t led_state_2 = 0xFF;

const uint8_t COL_PINS[4] = {0, 1, 6, 4};
const uint8_t ROW_PINS[4] = {2, 3, 5, 7};

uint8_t LED_MAP[16] = {1, 3, 4, 7, 0, 2, 5, 6, 0, 2, 4, 6, 1, 3, 5, 7};

const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                             66, 68, 70, -1, 67, 69, 71, 72};

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

HardwareManager::HardwareManager(Synth &synth_ref) : synth(synth_ref) {}

void HardwareManager::init() {

    for (int i = 0; i < NUM_ENCODERS; ++i) {
        init_encoder(&encoders[i]);
    }
    //
    // // Init display
    init_display();
}

void HardwareManager::init_display() {
    disp.external_vcc = false;
    ssd1306_init(&disp, 128, 64, 0x3C, i2c1);
    // ssd1306_hflip(&disp, 1);
    ssd1306_rotate(&disp, 1);
    ssd1306_clear(&disp);
    const char *words[] = {"SSD1306", "DISPLAY", "DRIVER"};
    ssd1306_draw_string(&disp, 8, 24, 1, words[0]);
    ssd1306_show(&disp);
}

void HardwareManager::update() {
    poll_inputs();
    update_display();
}

void HardwareManager::poll_inputs() {
    handle_encoders();
    handle_keypad();
}

void HardwareManager::handle_encoders() {
    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &encoders[i];
        int32_t count = quadrature_encoder_get_count(enc->pio, enc->sm);
        int32_t delta = count - enc->last_count;

        if (delta != 0 && abs(delta) > 1) {
            enc->last_count = count;

            switch (i) {
            case 0:
                synth.cycle_wave_type(delta > 0 ? 1 : -1);
                break;

            case 1: {
                q8_24_t increment = q24_from_float(.1f);
                for (auto &env : synth.envelopes) {
                    env.increment_ADSR(current_adsr_param,
                                       delta > 0 ? increment : -increment);
                }
                adsr_dirty = true;
                break;
            }
            case 2:
                // Only adjust filter cutoff if not in FILTER_OFF mode
                if (synth.current_filter_type != FILTER_OFF) {
                    float cut_off = synth.get_filter_cutoff();
                    float new_cut_off = cut_off + (delta > 0 ? 50.f : -50.f);
                    // Ensure cutoff stays within reasonable bounds
                    new_cut_off = new_cut_off < 20.0f ? 20.0f : new_cut_off;
                    new_cut_off =
                        new_cut_off > 20000.0f ? 20000.0f : new_cut_off;
                    synth.set_filter_cutoff(new_cut_off, 0.5f);
                }
                filter_dirty = true;
                break;
            }
        }

        // Handle button press (edge detect)
        bool current_btn = gpio_get(enc->sw_pin);
        if (i == 1 && !current_btn && last_encoder1_button) {
            current_adsr_param = (current_adsr_param + 1) % 4;
            adsr_dirty = true;
        }
        if (i == 1)
            last_encoder1_button = current_btn;

        // Add filter type cycling on encoder 2's button
        if (i == 2 && !current_btn && last_encoder2_button) {
            synth.cycle_filter_type();
            filter_dirty = true;
        }
        if (i == 2) {
            last_encoder2_button = current_btn;
        }
    }
}

void HardwareManager::handle_keypad() {
    uint16_t curr = scan_key_state(i2c0);
    KeyChanges changes = compute_key_changes(prev_keys, curr);
    update_leds_from_keys(i2c1, prev_keys, curr);

    for (int i = 0; i < 16; ++i) {
        if ((changes.note_on_mask >> i) & 1) {
            uint8_t note = key_to_midi[i];
            if (note != 255)
                synth.note_on(note, 127);
        }
        if ((changes.note_off_mask >> i) & 1) {
            uint8_t note = key_to_midi[i];
            if (note != 255)
                synth.note_off(note, 0);
        }
    }

    prev_keys = curr;
}

void HardwareManager::update_leds(uint16_t prev, uint16_t curr) {
    update_leds_from_keys(i2c1, prev, curr); // use your existing helper
}

void HardwareManager::update_display() {
    bool changed = false;
    std::bitset<128> note_state = synth.get_notes_bitmask();
    if (note_state != last_note_state) {
        last_note_state = note_state;
        draw_notes();
        changed = true;
    }

    WaveType current = synth.oscillators[0].get_wave_type();
    if (current != last_wave_type) {
        last_wave_type = current;
        draw_wave_type();
        changed = true;
    }

    if (adsr_dirty) {
        draw_adsr(); // new function below
        changed = true;
        adsr_dirty = false;
    }

    if (filter_dirty) {
        draw_filter();
        changed = true;
        filter_dirty = false;
    }

    if (changed) {
        ssd1306_show(&disp);
    }
}

void HardwareManager::draw_notes() {
    ssd1306_clear_square(&disp, 8, 24, 120, 8);
    ssd1306_draw_string(&disp, 8, 24, 1, synth.get_notes_playing_names());
}

void HardwareManager::draw_wave_type() {
    ssd1306_clear_square(&disp, 56, 0, 64, 8);
    ssd1306_draw_string(&disp, 8, 0, 1, "Wave:");
    ssd1306_draw_string(&disp, 56, 0, 1, wave_type_to_string(last_wave_type));
}

void HardwareManager::draw_adsr() {
    ssd1306_clear_square(&disp, 0, 36, 128, 16); // 2 lines tall

    char values[4][8];
    synth.envelopes[0].get_ADSR_strings(values); // 2 decimal digits

    // Draw parameter strings starting at x=8
    char line1[24], line2[24];
    snprintf(line1, sizeof(line1), "A:%s   D:%s", values[0], values[1]);
    snprintf(line2, sizeof(line2), "S:%s   R:%s", values[2], values[3]);
    ssd1306_draw_string(&disp, 8, 36, 1, line1);
    ssd1306_draw_string(&disp, 8, 44, 1, line2);

    // Draw arrow at one of four fixed positions based on current_adsr_param
    int arrow_x, arrow_y;

    switch (current_adsr_param) {
    case 0: // A
        arrow_x = 0;
        arrow_y = 36;
        break;
    case 1: // D
        arrow_x = 52;
        arrow_y = 36;
        break;
    case 2: // S
        arrow_x = 0;
        arrow_y = 44;
        break;
    case 3: // R
        arrow_x = 52;
        arrow_y = 44;
        break;
    }

    // Draw the arrow character
    ssd1306_draw_char(&disp, arrow_x, arrow_y, 1, '>');
}

void HardwareManager::draw_filter() {
    char fc_value[32];

    // Display different information based on filter type
    switch (synth.current_filter_type) {
    case FILTER_LOW_PASS:
        snprintf(fc_value, sizeof(fc_value), "LP: %.1f Hz",
                 synth.get_filter_cutoff());
        break;
    case FILTER_CHEBYSHEV:
        snprintf(fc_value, sizeof(fc_value), "Cheb: %.1f Hz",
                 synth.get_filter_cutoff());
        break;
    default: // off
        snprintf(fc_value, sizeof(fc_value), "Filter: OFF");
        break;
    }

    ssd1306_clear_square(&disp, 0, 8, 128, 8); // Clear the entire line
    ssd1306_draw_string(&disp, 8, 8, 1, fc_value);
}
