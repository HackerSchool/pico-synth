#ifndef HARDWARE_MANAGER
#define HARDWARE_MANAGER

#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "i2s_init.hpp"
#include "quadrature_encoder.pio.h"
#include "tusb.h"
#include <cstdint>

#define PCF8574_KEYPAD_ADDR 0x20
#define PCF8574_LED_ADDR_1 0x20
#define PCF8574_LED_ADDR_2 0x21
#define NUM_ENCODERS 4

// Struct for each encoder's runtime state
typedef struct {
    int32_t last_count;
    uint clk_pin;
    uint sw_pin;
    PIO pio;
    uint sm;
} Encoder;

extern Encoder encoders[NUM_ENCODERS];

// Initialize a quadrature encoder PIO state machine
void init_encoder(Encoder *enc);

// Get count from the encoder
// inline int32_t get_encoder_count(PIO pio, uint sm);

uint16_t scan_key_state(i2c_inst_t *i2c);

void update_led(i2c_inst_t *i2c, int key, bool on);

void update_leds_from_keys(i2c_inst_t *i2c, uint16_t prev_state,
                           uint16_t curr_state);

#endif // !HARDWARE_MANAGER
