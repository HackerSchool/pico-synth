#include "Ui.hpp"
#include "HardwareManager.hpp"
#include "MidiHandler.hpp"
#include "Synth.hpp"

const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                             66, 68, 70, -1, 67, 69, 71, 72};

UiHandler::UiHandler(Synth &synth_ref, HardwareManager &hw,
                     MidiHandler &midi_handler)
    : synth(synth_ref), hw(hw), midi(midi_handler) {}

void UiHandler::update() {
    UiDispatchEntry ui_dispatch_entry = ui_dispatch_table[ui_state];
    ui_dispatch_entry.handle_encoders(*this, synth, hw);
    ui_dispatch_entry.handle_switches(*this);
    ui_dispatch_entry.handle_display(*this);
}

void UiHandler::main_handle_switches(UiHandler &self) {

    Synth &synth = self.synth;
    MidiHandler &midi = self.midi;

    uint16_t curr = self.hw.curr_switches;
    KeyChanges changes = compute_key_changes(self.prev_switches, curr);
    update_leds_from_keys(i2c1, self.prev_switches, curr);

    for (int i = 0; i < 16; ++i) {
        if ((changes.note_on_mask >> i) & 1) {
            uint8_t note = key_to_midi[i];
            if (note != 255) {
                if (self.switches_in)
                    synth.note_on(note, 127);
                if (self.midi_out)
                    midi.midi_send_note(note, 127, true);
            }
        }
        if ((changes.note_off_mask >> i) & 1) {
            uint8_t note = key_to_midi[i];
            if (note != 255) {
                if (self.switches_in)
                    synth.note_off(note, 0);
                if (self.midi_out)
                    midi.midi_send_note(note, 0, false);
            }
        }
    }

    self.prev_switches = curr;
}
