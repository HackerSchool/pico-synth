#ifndef HARDWARE_MANAGER
#define HARDWARE_MANAGER

#include "Synth.hpp"
#include "Wavetable.hpp"
#include "hardware/i2c.h"
#include "hardware/pio.h"
// #include "i2s_init.hpp"
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

    int32_t delta;
    bool button_state;
    bool button_edge;

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

    Encoder encoders[NUM_ENCODERS] = {
        {.last_count = 0,
         .clk_pin = 10,
         .sw_pin = 9,
         .pio = pio0,
         .sm = 0,
         .delta = 0,
         .button_state = 0,
         .button_edge = 0},
        {.last_count = 0,
         .clk_pin = 7,
         .sw_pin = 6,
         .pio = pio0,
         .sm = 1,
         .delta = 0,
         .button_state = 0,
         .button_edge = 0},
        {.last_count = 0,
         .clk_pin = 4,
         .sw_pin = 3,
         .pio = pio0,
         .sm = 2,
         .delta = 0,
         .button_state = 0,
         .button_edge = 0},
        {.last_count = 0,
         .clk_pin = 1,
         .sw_pin = 0,
         .pio = pio0,
         .sm = 3,
         .delta = 0,
         .button_state = 0,
         .button_edge = 0},
    };

    // int current_adsr_param = 0; // 0=A, 1=D, 2=S, 3=R
    // bool adsr_dirty = 0;
    // bool filter_dirty = 0;

    uint16_t curr_switches = 0;

    void draw_notes();
    void draw_wave_type(WaveType wave_type);
    void draw_adsr(int current_adsr_param);
    void draw_midi_settings(bool midi_out, bool midi_in, bool switches_in,
                            bool sequencer_in, bool sequencer_out);
    void draw_filter();

    void draw_sequencer_settings(bool playing, uint32_t tempo,
                                 uint8_t current_step);
    void display_show();

  private:
    Synth &synth;

    std::bitset<128> last_note_state;
    WaveType last_wave_type = static_cast<WaveType>(-1);

    // Helpers
    void poll_encoders();
    void poll_keypad();
    void update_leds(uint16_t prev, uint16_t curr);

    // uint16_t prev_state = 0;

    // Display
    ssd1306_t disp;

    void init_display();
    // bool last_encoder1_button = true; // for edge detection
    // bool last_encoder2_button = true; // for edge detection
};

#endif // !HARDWARE_MANAGER
//
