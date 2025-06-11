#include "HardwareManager.hpp"
#include "MidiHandler.hpp"
#include "Ui.hpp"
#include "fixed_point.h"
#include <cstdint>
#include <cstdio>

const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                             66, 68, 70, -1, 67, 69, 71, 72};

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
                    packet[2] = note + 12 * self.octave;
                    packet[3] = 0x7F; // Velocity
                    midi.midi_receive_note(packet);
                }
                if (self.midi_out)
                    midi.midi_send_note(note, 127, true);
            } else {
                printf("Change Octave button: %d", i);
                switch (i) {
                case 0:
                    if (self.octave > -5) {
                        self.octave--;
                        self.channel_dirty = true;
                    }
                    break;
                case 11:
                    if (self.octave < 4) {
                        self.octave++;
                        self.channel_dirty = true;
                    }
                    break;
                default:
                    break;
                }
            }
        }
        if ((changes.note_off_mask >> i) & 1) {
            uint8_t note = key_to_midi[i];
            if (note != 255) {
                if (self.switches_in) {
                    uint8_t packet[4];
                    packet[0] = 0x08; // CIN = Note Off, Cable 0
                    packet[1] = 0x80 | (self.midi_channel & 0x0F);
                    packet[2] = note + 12 * self.octave;
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

void UiHandler::main_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;
    MidiHandler &midi = self.midi;

    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        int32_t delta = enc->delta;

        if (delta != 0 && abs(delta) > 1) {
            switch (i) {
            case 0:
                self.midi_channel =
                    (self.midi_channel + (delta > 0 ? 1 : 15)) & 0x0F;

                self.channel_dirty = true;
                break;

            case 1: {
                // Adjust currently selected ADSR param
                int param = self.current_adsr_param; // 0=A, 1=D, 2=S, 3=R
                int16_t value = self.adsr[param] + (delta > 0 ? 1 : -1);
                if (value < 0)
                    value = 0;
                if (value > 127)
                    value = 127;
                self.adsr[param] = value;

                const uint8_t cc_map[4] = {73, 75, 70,
                                           72}; // MIDI CCs for A, D, S, R
                uint8_t packet[4] = {0x0B, 0xB0, cc_map[param],
                                     static_cast<uint8_t>(value)};
                midi.midi_receive_note(packet);
                self.adsr_dirty = true;
                break;
            }
            case 2:
                // Only adjust filter cutoff if not in FILTER_OFF mode
                // if (synth.current_filter_type != FILTER_OFF) {
                //     float cut_off = synth.get_filter_cutoff();
                //     float new_cut_off = cut_off + (delta > 0 ? 50.f : -50.f);
                //     // Ensure cutoff stays within reasonable bounds
                //     new_cut_off = new_cut_off < 20.0f ? 20.0f : new_cut_off;
                //     new_cut_off =
                //         new_cut_off > 20000.0f ? 20000.0f : new_cut_off;
                //     synth.set_filter_cutoff(new_cut_off, 0.5f);
                //     // printf("new cut off %f\n", new_cut_off);
                // }
                // TODO: filter cutoff midi
                self.filter_dirty = true;
                break;
            }
        }

        if (!enc->button_state && enc->button_edge) {
            if (i == 1) {

                self.current_adsr_param = (self.current_adsr_param + 1) % 4;
                self.adsr_dirty = true;
            }
            if (i == 2) {

                // synth.cycle_filter_type();
                // TODO: filter type midi
                self.filter_dirty = true;
            }

            if (i == 3) {
                self.ui_state = UI_STATE_MIDI_SETTINGS;
                printf("State: MIDI_STATE");
            }
        }
    }
}

void UiHandler::main_update_display(UiHandler &self) {
    // Synth &synth = self.synth;
    HardwareManager &hw = self.hw;
    bool changed = false;

    // std::bitset<128> note_state = synth.get_notes_bitmask();
    // if (note_state != self.last_note_state) {
    //     self.last_note_state = note_state;
    //     hw.draw_notes();
    //     changed = true;
    // }
    //
    if (self.channel_dirty) {
        hw.draw_wave_type(self.midi_channel, self.octave);
        changed = true;
    }
    // TODO: get synth state on the UI

    if (self.adsr_dirty) {
        hw.draw_adsr(self.current_adsr_param, self.adsr[0], self.adsr[1],
                     self.adsr[2], self.adsr[3]);
        changed = true;
        self.adsr_dirty = false;
    }

    if (self.filter_dirty) {
        hw.draw_filter();
        changed = true;
        self.filter_dirty = false;
    }

    if (changed) {
        // ssd1306_show(&disp);
        hw.display_show();
    }
}
