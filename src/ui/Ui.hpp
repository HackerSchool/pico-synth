#ifndef UI_STATE_HPP
#define UI_STATE_HPP

#include "HardwareManager.hpp"
#include "MidiHandler.hpp"
#include "Synth.hpp"
#include <cstdint>

typedef enum UiState {
    UI_STATE_MAIN,
    UI_STATE_MIDI_SETTINGS,
    UI_STATE_SEQUENCER,
    UI_STATE_COUNT // helpful for bounds checking
} UiState;

class UiHandler;

typedef void (*EncoderHandler)(UiHandler &, Synth &, HardwareManager &);
typedef void (*SwitchHandler)(UiHandler &);
typedef void (*DisplayHandler)(UiHandler &);

typedef struct {
    EncoderHandler handle_encoders;
    SwitchHandler handle_switches;
    DisplayHandler handle_display;
} UiDispatchEntry;

class UiHandler {

  public:
    UiHandler(Synth &synth_ref, HardwareManager &hw, MidiHandler &midi_handler);

    // void init();
    void update(); // called every loop
    //
    // main state
    static void main_handle_encoders(UiHandler &self, Synth &synth,
                                     HardwareManager &hw);
    static void main_handle_switches(UiHandler &self);
    static void main_update_display(UiHandler &self);

    // midi state
    static void midi_handle_encoders(UiHandler &self, Synth &synth,
                                     HardwareManager &hw);

    static void midi_update_display(UiHandler &self);

  private:
    Synth &synth;
    HardwareManager &hw;
    MidiHandler &midi;

    UiState ui_state = UI_STATE_MAIN;

    UiDispatchEntry ui_dispatch_table[UI_STATE_COUNT] = {
        [UI_STATE_MAIN] = {.handle_encoders = main_handle_encoders,
                           .handle_switches = main_handle_switches,
                           .handle_display = main_update_display},
        [UI_STATE_MIDI_SETTINGS] = {.handle_encoders = midi_handle_encoders,
                                    .handle_switches = main_handle_switches,
                                    .handle_display = midi_update_display},
        [UI_STATE_SEQUENCER] = {.handle_encoders = nullptr,
                                .handle_switches = nullptr,
                                .handle_display = nullptr}};

    // shit I need for the main state
    int current_adsr_param = 0; // 0=A, 1=D, 2=S, 3=R
    std::bitset<128> last_note_state;
    uint16_t prev_switches = 0;
    WaveType last_wave_type = static_cast<WaveType>(-1);
    bool adsr_dirty = 0;
    bool filter_dirty = 0;

    // midi state
    bool midi_in = false; //midi into pico synth
    bool midi_out = false; //switches to midi out
    bool switches_in = true; //switches into pico synth
    bool sequencer_in = true; //sequencer synth in
    bool sequencer_out = true; //sequencer midi out
    bool midi_settings_dirty = false; //sequencer midi out


};

#endif // !UI_STATE_HPP
