#ifndef UI_STATE_HPP
#define UI_STATE_HPP

#include "HardwareManager.hpp"
#include "MidiHandler.hpp"
#include "Sampler.hpp"
#include "Sequencer.hpp"
#include "Synth.hpp"
#include <cstdint>

// When adding a new state here watch out for the order
// sine enums are basically ints
typedef enum UiState {
    UI_STATE_MAIN,          // = 0
    UI_STATE_MIDI_SETTINGS, // = 1
    UI_STATE_SEQUENCER, // = 2
    UI_STATE_SAMPLER, // = 3
    UI_STATE_SEQUENCER_EDIT,
    UI_STATE_CHOOSE,
    UI_STATE_COUNT // helpful for bounds checking
} UiState;

#define NUM_USABLE_STATES (4) //4 states (MAIN, MIDI, SEQUENCER, SAMPLER) (states listed in the menu)

class UiHandler;

typedef void (*EncoderHandler)(UiHandler &);
typedef void (*SwitchHandler)(UiHandler &);
typedef void (*DisplayHandler)(UiHandler &);

typedef struct {
    EncoderHandler handle_encoders;
    SwitchHandler handle_switches;
    DisplayHandler handle_display;
} UiDispatchEntry;

class UiHandler {

  public:
    UiHandler(HardwareManager &hw, MidiHandler &midi_handler, Sequencer &seq,
              Sampler &sampler, Synth &synth); 

    // void init();
    void update(); // called every loop
    //
    // main state
    static void main_handle_encoders(UiHandler &self);
    static void main_handle_switches(UiHandler &self);
    static void main_update_display(UiHandler &self);

    // midi state
    static void midi_handle_encoders(UiHandler &self);
    static void midi_update_display(UiHandler &self);

    // sequencer state
    static void sequencer_handle_encoders(UiHandler &self);
    static void sequencer_update_display(UiHandler &self);

    // sequencer edit state
    static void sequencer_note_edit_handle_switches(UiHandler &self);
    static void sequencer_note_edit_handle_encoders(UiHandler &self);
    static void sequencer_note_edit_update_display(UiHandler &self);
    void sequencer_note_edit_enter(UiHandler &self);

    // choose state
    static void choose_handle_encoders(UiHandler &self);
    static void choose_update_display(UiHandler &self);

    // sampler state
    static void sampler_handle_encoders(UiHandler &self);
    static void sampler_update_display(UiHandler &self);

    // helpers:

    void set_adsr_param(int param, uint8_t value);
    uint8_t get_adsr_param(int param);

  private:
    HardwareManager &hw;
    MidiHandler &midi;
    Sequencer &seq;
    Sampler &sampler;
    Synth &synth;

    UiState ui_state = UI_STATE_MAIN;

    UiDispatchEntry ui_dispatch_table[UI_STATE_COUNT] = {
        [UI_STATE_MAIN] = {.handle_encoders = main_handle_encoders,
                           .handle_switches = main_handle_switches,
                           .handle_display = main_update_display},
        [UI_STATE_MIDI_SETTINGS] = {.handle_encoders = midi_handle_encoders,
                                    .handle_switches = main_handle_switches,
                                    .handle_display = midi_update_display},
        [UI_STATE_SEQUENCER] = {.handle_encoders = sequencer_handle_encoders,
                                .handle_switches = main_handle_switches,
                                .handle_display = sequencer_update_display},
        [UI_STATE_SAMPLER] = {.handle_encoders = sampler_handle_encoders, 
                              .handle_switches = main_handle_switches,
                              .handle_display = sampler_update_display},
        [UI_STATE_SEQUENCER_EDIT] = {.handle_encoders = sequencer_note_edit_handle_encoders,
                                     .handle_switches = sequencer_note_edit_handle_switches,
                                     .handle_display = sequencer_note_edit_update_display},
        [UI_STATE_CHOOSE] = {.handle_encoders = choose_handle_encoders, // <- CORRECTED: Removed the '=' after UI_STATE_CHOOSE
                             .handle_switches = main_handle_switches,
                             .handle_display = choose_update_display}}; // The semicolon only belongs here, at the end of the entire array initialization block.

    // shit I need for the main state
    int current_adsr_param = 0; // 0=A, 1=D, 2=S, 3=R
    // Channel-specific parameters (16 MIDI channels)

    // std::array<ChannelParams, 16> channel_params;

    uint8_t adsr[4] = {64, 64, 64,
                       64}; // Attack, Decay, Sustain, Release (MIDI 7-bit)
    std::bitset<128> last_note_state;
    uint16_t prev_switches = 0;
    WaveType last_wave_type = static_cast<WaveType>(-1);
    bool adsr_dirty = 1;
    bool channel_dirty = 1;
    uint8_t midi_channel = 0;
    int8_t octave = 0;

    uint8_t filter_cutoff_msb = 64; // Default mid-range
    uint8_t filter_cutoff_lsb = 0;
    uint8_t filter_q_msb = 16; // Default lower Q
    uint8_t filter_q_lsb = 0;
    int8_t filter_type = 0; // 0=Off, 1=FIR, 2=Cheby
    bool filter_dirty = 1;
    bool main_dirty = 1;

    // midi state
    bool midi_in = false;             // midi into pico synth
    bool midi_out = false;            // switches to midi out
    bool switches_in = true;          // switches into pico synth
    bool sequencer_in = true;         // sequencer synth in
    bool sequencer_out = true;        // sequencer midi out
    bool midi_settings_dirty = false; // sequencer midi out
    //

    // sequencer state
    bool sequencer_settings_dirty = true;
    bool sequencer_playing = false;
    uint32_t display_tempo = 120;
    uint8_t display_current_step = 0;

    // sequencer edit state
    uint8_t current_sequencer_step = 0;
    bool auto_stepping_enabled = false;
    bool sequencer_dirty = true;

    // choose state
    bool chosen_dirty = true;
    int chosen_index = 1;

    //sampler state
    bool sampler_dirty = true;
    int sample_index = 0;
    int sample_channel = 6;
    // WAV file list for sampler
    WavFileList wav_files;
};

#endif // !UI_STATE_HPP
