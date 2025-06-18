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
                // if (self.midi_out)
                //     midi.midi_send_note(note, 12 * self.octave, 127, true);
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
                self.adsr_dirty = true;
                break;

            case 1: {
                int param = self.current_adsr_param;
                int16_t value =
                    self.get_adsr_param(param) + (delta > 0 ? 1 : -1);
                if (value < 0)
                    value = 0;
                if (value > 127)
                    value = 127;

                self.set_adsr_param(param, static_cast<uint8_t>(value));

                const uint8_t cc_map[4] = {73, 75, 70, 72};
                uint8_t packet[4] = {
                    0x0B, static_cast<uint8_t>(0xB0 | self.midi_channel),
                    cc_map[param], static_cast<uint8_t>(value)};
                midi.midi_receive_note(packet);
                self.adsr_dirty = true;
                break;
            }
            case 2: {
                // Filter cutoff control with 14-bit resolution
                int32_t current_cutoff_14bit =
                    (self.filter_cutoff_msb << 7) | self.filter_cutoff_lsb;
                int32_t new_cutoff_14bit =
                    current_cutoff_14bit +
                    (delta > 0 ? 100 : -100); // Adjust step size as needed

                // Clamp to 14-bit range
                if (new_cutoff_14bit < 0)
                    new_cutoff_14bit = 0;
                if (new_cutoff_14bit > 16383)
                    new_cutoff_14bit = 16383;

                // Update MSB and LSB
                self.filter_cutoff_msb = (new_cutoff_14bit >> 7) & 0x7F;
                self.filter_cutoff_lsb = new_cutoff_14bit & 0x7F;

                // Send MIDI CC messages for both MSB and LSB
                uint8_t packet_msb[4] = {
                    0x0B, static_cast<uint8_t>(0xB0 | self.midi_channel), 16,
                    self.filter_cutoff_msb // CC 16 = Cutoff MSB
                };
                uint8_t packet_lsb[4] = {
                    0x0B, static_cast<uint8_t>(0xB0 | self.midi_channel), 48,
                    self.filter_cutoff_lsb // CC 48 = Cutoff LSB
                };

                midi.midi_receive_note(packet_msb);
                midi.midi_receive_note(packet_lsb);
                self.filter_dirty = true;
                break;
            }
            }
        }

        if (!enc->button_state && enc->button_edge) {
            if (i == 1) {

                self.current_adsr_param = (self.current_adsr_param + 1) % 4;
                self.adsr_dirty = true;
            }
            if (i == 2) {
                // Cycle through filter types: Off -> FIR -> Chebyshev -> Off...
                self.filter_type = (self.filter_type + 1) % 3;

                // Map to MIDI values: 0-42=Off, 43-84=FIR, 85-127=Cheby
                uint8_t midi_filter_value;
                switch (self.filter_type) {
                case 0:
                    midi_filter_value = 21;
                    break; // Off (middle of 0-42 range)
                case 1:
                    midi_filter_value = 64;
                    break; // FIR (middle of 43-84 range)
                case 2:
                    midi_filter_value = 106;
                    break; // Cheby (middle of 85-127 range)
                }

                uint8_t packet[4] = {
                    0x0B, static_cast<uint8_t>(0xB0 | self.midi_channel), 18,
                    midi_filter_value // CC 18 = Filter Type
                };

                midi.midi_receive_note(packet);
                self.filter_dirty = true;
            }

            if (i == 3) {
                self.ui_state = UI_STATE_MIDI_SETTINGS;
                self.midi_settings_dirty = true;
                printf("State: MIDI_STATE");
            }
        }
    }
}

void UiHandler::main_update_display(UiHandler &self) {
    // Synth &synth = self.synth;
    HardwareManager &hw = self.hw;
    bool changed = false;

    if (self.main_dirty) {
        hw.display_clear();
        self.main_dirty = false;
    }

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
        uint8_t midi_channel = self.midi_channel;
        hw.draw_adsr(self.current_adsr_param,
                     self.channel_params[midi_channel].attack,
                     self.channel_params[midi_channel].decay,
                     self.channel_params[midi_channel].sustain >> 8,
                     self.channel_params[midi_channel].release);
        changed = true;
        self.adsr_dirty = false;
    }

    if (self.filter_dirty) {
        // printf("YOOOOO\n");
        hw.draw_filter(self.filter_type, 100.f);
        changed = true;
        self.filter_dirty = false;
    }

    if (changed) {
        // ssd1306_show(&disp);
        hw.display_show();
    }
}
