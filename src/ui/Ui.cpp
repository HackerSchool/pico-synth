#include "Ui.hpp"
#include "HardwareManager.hpp"
#include "MidiHandler.hpp"
#include "Sequencer.hpp"
#include "Synth.hpp"

const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                             66, 68, 70, -1, 67, 69, 71, 72};

UiHandler::UiHandler(HardwareManager &hw, MidiHandler &midi_handler,
                     Sequencer &seq)
    : hw(hw), midi(midi_handler), seq(seq) {}

void UiHandler::update() {
    UiDispatchEntry ui_dispatch_entry = ui_dispatch_table[ui_state];
    ui_dispatch_entry.handle_encoders(*this);
    ui_dispatch_entry.handle_switches(*this);
    ui_dispatch_entry.handle_display(*this);
}

void UiHandler::main_handle_switches(UiHandler &self) {

    MidiHandler &midi = self.midi;

    uint16_t curr = self.hw.curr_switches;
    KeyChanges changes = compute_key_changes(self.prev_switches, curr);
    update_leds_from_keys(i2c1, self.prev_switches, curr);

    for (int i = 0; i < 16; ++i) {
        if ((changes.note_on_mask >> i) & 1) {
            uint8_t note = key_to_midi[i];
            if (note != 255) {
                if (self.switches_in) {
                    uint8_t packet[4];
                    packet[0] = 0x09; // CIN = Note On, Cable 0
                    packet[1] = 0x90 | (self.midi_channel & 0x0F); // Status
                    packet[2] = note;
                    packet[3] = 0x7F; // Velocity
                    midi.midi_receive_note(packet);
                }
                if (self.midi_out)
                    midi.midi_send_note(note, 127, true);
            }
        }
        if ((changes.note_off_mask >> i) & 1) {
            uint8_t note = key_to_midi[i];
            if (note != 255) {
                if (self.switches_in) {
                    uint8_t packet[4];
                    packet[0] = 0x08; // CIN = Note Off, Cable 0
                    packet[1] = 0x80 | (self.midi_channel & 0x0F);
                    packet[2] = note;
                    packet[3] = 0x7F; // Velocity
                    midi.midi_receive_note(packet);
                }
                if (self.midi_out)
                    midi.midi_send_note(note, 0, false);
            }
        }
    }

    self.prev_switches = curr;
}
