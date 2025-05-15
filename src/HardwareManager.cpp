#include "HardwareManager.hpp"

uint8_t led_state_1 = 0xFF;
uint8_t led_state_2 = 0xFF;

const uint8_t COL_PINS[4] = {0, 1, 6, 4};
const uint8_t ROW_PINS[4] = {2, 3, 5, 7};

uint8_t LED_MAP[16] = {1, 3, 4, 7, 0, 2, 5, 6, 0, 2, 4, 6, 1, 3, 5, 7};

Encoder encoders[NUM_ENCODERS] = {
    {.last_count = 0, .clk_pin = 10, .sw_pin = 9, .pio = pio0, .sm = 0},
    {.last_count = 0, .clk_pin = 7, .sw_pin = 6, .pio = pio0, .sm = 1},
    {.last_count = 0, .clk_pin = 4, .sw_pin = 3, .pio = pio0, .sm = 2},
    {.last_count = 0, .clk_pin = 1, .sw_pin = 0, .pio = pio0, .sm = 3},
};

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

    // for (int i = 0; i < 16; ++i) {
    //     if (!(changed & (1 << i)))
    //         continue;
    //
    //     int midi_note = key_to_midi[i];
    //     if (midi_note == -1)
    //         continue;
    //
    //     bool pressed = curr_state & (1 << i);
    //     if (pressed)
    //         changes.note_on_mask |= (1 << i);
    //     else
    //         changes.note_off_mask |= (1 << i);
    // }

    changes.note_on_mask = changed & curr_state;
    changes.note_off_mask = changed & prev_state;

    return changes;
}
