#ifndef HARDWARE_MANAGER
#define HARDWARE_MANAGER

#include "Synth.hpp"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "i2s_init.hpp"
#include "quadrature_encoder.pio.h"
#include "ssd1306.h"
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

struct KeyChanges {
    uint16_t note_on_mask;
    uint16_t note_off_mask;
};

KeyChanges compute_key_changes(uint16_t prev_state, uint16_t curr_state);

extern Encoder encoders[NUM_ENCODERS];

// Initialize a quadrature encoder PIO state machine
void init_encoder(Encoder *enc);

// Get count from the encoder
// inline int32_t get_encoder_count(PIO pio, uint sm);

uint16_t scan_key_state(i2c_inst_t *i2c);

void update_led(i2c_inst_t *i2c, int key, bool on);

void update_leds_from_keys(i2c_inst_t *i2c, uint16_t prev_state,
                           uint16_t curr_state);

KeyChanges compute_key_changes(uint16_t prev_state, uint16_t curr_state);

class HardwareManager {
  public:
    HardwareManager(Synth &synth_ref);

    void init();
    void update();      // called every loop
    void poll_inputs(); // handle encoders + buttons
    void update_display();

  private:
    Synth &synth;

    Encoder encoders[NUM_ENCODERS] = {
        {.last_count = 0, .clk_pin = 10, .sw_pin = 9, .pio = pio0, .sm = 0},
        {.last_count = 0, .clk_pin = 7, .sw_pin = 6, .pio = pio0, .sm = 1},
        {.last_count = 0, .clk_pin = 4, .sw_pin = 3, .pio = pio0, .sm = 2},
        {.last_count = 0, .clk_pin = 1, .sw_pin = 0, .pio = pio0, .sm = 3},
    };
    std::bitset<128> last_note_state;
    WaveType last_wave_type = static_cast<WaveType>(-1);
    uint16_t prev_keys = 0;

    // Helpers
    void handle_encoders();
    void handle_keypad();
    void update_leds(uint16_t prev, uint16_t curr);
    void draw_notes();
    void draw_wave_type();
    void draw_adsr();

    uint16_t prev_state = 0;

    // Display
    ssd1306_t disp;

    void init_display();
    int current_adsr_param = 0;       // 0=A, 1=D, 2=S, 3=R
    bool last_encoder1_button = true; // for edge detection
    bool adsr_dirty = 0;
};

#endif // !HARDWARE_MANAGER
//
