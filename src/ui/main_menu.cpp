#include "HardwareManager.hpp"
#include "MidiHandler.hpp"
#include "Sampler.hpp"
#include "Ui.hpp"
#include "fixed_point.h"
#include "pico/time.h"
#include <cstdint>
#include <cstdio>

const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                             66, 68, 70, -1, 67, 69, 71, 72};

void UiHandler::main_handle_switches(UiHandler &self) {
    if (self.synth.get_engine() == SynthEngine::KarplusStrong) {
        karplus_edit_handle_switches(self);
        return;
    }
    if (self.synth.get_engine() == SynthEngine::Modal) {
        modal_edit_handle_switches(self);
        return;
    }

    uint16_t curr = self.hw.curr_switches;
    KeyChanges changes = compute_key_changes(self.prev_switches, curr);
    update_leds_from_keys(i2c1, self.prev_switches, curr);

    for (int i = 0; i < 16; ++i) {
        if ((changes.note_on_mask >> i) & 1) {
            if (i == 3) {
                self.begin_randomizer_hold();
                continue;
            }

            uint8_t note = key_to_midi[i];
            if (note != 255) {
                if (self.switches_in) {
                    const uint8_t midi_note =
                        static_cast<uint8_t>(note + (12 * self.octave));
                    self.track_switch_note_on(i, self.midi_channel, midi_note);
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
            if (i == 3) {
                self.end_randomizer_hold();
                continue;
            }

            uint8_t note = key_to_midi[i];
            if (note != 255) {
                self.release_tracked_switch_note(i);
                if (self.midi_out)
                    self.midi.midi_send_note(note, 0, false);
            }
        }
    }

    self.prev_switches = curr;
}

void UiHandler::main_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;
    MidiHandler &midi = self.midi;
    Synth &synth = self.synth;

    if (synth.get_engine() == SynthEngine::KarplusStrong) {
        karplus_edit_handle_encoders(self);
        return;
    }
    if (synth.get_engine() == SynthEngine::Modal) {
        modal_edit_handle_encoders(self);
        return;
    }

    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        int32_t delta = enc->delta;

        if (UiHandler::encoder_moved(delta)) {
            switch (i) {
            case 0:
                self.midi_channel = static_cast<uint8_t>(
                    (self.midi_channel + UiHandler::encoder_velocity_delta(delta, 1, 2, 4) + 16) & 0x0F);

                self.channel_dirty = true;
                self.adsr_dirty = true;
                break;

            case 1: {
                if (synth.get_engine() != SynthEngine::FM) {
                    break;
                }

                Patch &patch = synth.patch_storage[self.midi_channel];
                int new_ratio = static_cast<int>(patch.ops[1].ratio) +
                                UiHandler::encoder_velocity_delta(delta, 1, 2, 4);
                if (new_ratio < 1) new_ratio = 1;
                if (new_ratio > 16) new_ratio = 16;
                patch.ops[1].ratio = static_cast<uint16_t>(new_ratio);
                self.mark_fm_patch_updated(self.midi_channel);
                // int param = self.current_adsr_param;
                // // int16_t value =
                // //     self.get_adsr_param(param) + (delta > 0 ? 1 : -1);
                // // if (value < 0)
                // //     value = 0;
                // // if (value > 127)
                // //     value = 127;
                //
                // // self.set_adsr_param(param, static_cast<uint8_t>(value));
                //
                // const uint8_t cc_map[4] = {73, 75, 70, 72};
                // uint8_t packet[4] = {
                //     0x0B, static_cast<uint8_t>(0xB0 | self.midi_channel),
                //     cc_map[param], static_cast<uint8_t>(value)};
                // midi.midi_receive_note(packet);
                // self.adsr_dirty = true;
                break;
            }
            case 2: {
                // Filter cutoff control with 14-bit resolution
                int32_t current_cutoff_14bit =
                    (self.filter_cutoff_msb << 7) | self.filter_cutoff_lsb;
                int32_t new_cutoff_14bit =
                    current_cutoff_14bit +
                    UiHandler::encoder_velocity_delta(delta, 100, 500, 1000);

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
            if (i == 0 && synth.get_engine() == SynthEngine::FM) {
                self.release_all_tracked_switch_notes();
                self.ui_state = UI_STATE_FM_EDIT;
                self.fm_edit_dirty = true;
                continue;
            }
            if (i == 1) {

                self.current_adsr_param = (self.current_adsr_param + 1) % 4;
                self.adsr_dirty = true;
            }
            if (i == 2) {
                // Cycle through filter types: Off -> FIR -> Chebyshev -> Off...
                self.filter_type = (self.filter_type + 1) % 3;

                // Map to MIDI values: 0-42=Off, 43-84=FIR, 85-127=Cheby
                uint8_t midi_filter_value = 21;
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
                default:
                    break;
                }

                uint8_t packet[4] = {
                    0x0B, static_cast<uint8_t>(0xB0 | self.midi_channel), 18,
                    midi_filter_value // CC 18 = Filter Type
                };

                midi.midi_receive_note(packet);
                self.filter_dirty = true;
            }

            if (i == 3) {
                self.release_all_tracked_switch_notes();
                self.ui_state = UI_STATE_CHOOSE;
                self.chosen_index = UI_STATE_MAIN;
                self.main_dirty = true;
                self.chosen_dirty = true;
                printf("State: CHOOSE_STATE");
            }
        }
    }
}

void UiHandler::main_update_display(UiHandler &self) {
    if (self.preset_browse_overlay_active()) {
        return;
    }

    if (self.synth.get_engine() == SynthEngine::KarplusStrong) {
        karplus_edit_update_display(self);
        return;
    }
    if (self.synth.get_engine() == SynthEngine::Modal) {
        modal_edit_update_display(self);
        return;
    }

    // Synth &synth = self.synth;
    HardwareManager &hw = self.hw;
    bool changed = false;

    if (self.main_dirty) {
        hw.display_clear();
        self.main_dirty = false;
    }

    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    if (now_ms - self.waveform_animation_last_ms >= 120) {
        self.waveform_animation_last_ms = now_ms;
        self.waveform_animation_phase =
            static_cast<uint16_t>((self.waveform_animation_phase + 24) %
                                  WAVE_TABLE_LEN);
        self.channel_dirty = true;
    }

    // std::bitset<128> note_state = synth.get_notes_bitmask();
    // if (note_state != self.last_note_state) {
    //     self.last_note_state = note_state;
    //     hw.draw_notes();
    //     changed = true;
    // }
    //
    if (self.channel_dirty) {
        hw.draw_wave_type(self.midi_channel, self.octave,
                          self.waveform_animation_phase);
        self.channel_dirty = false;
        changed = true;
    }
    // TODO: get synth state on the UI

    if (self.adsr_dirty) {
        // uint8_t midi_channel = self.midi_channel;
        // hw.draw_adsr(self.current_adsr_param,
        //              self.channel_params[midi_channel].attack,
        //              self.channel_params[midi_channel].decay,
        //              self.channel_params[midi_channel].sustain >> 8,
        //              self.channel_params[midi_channel].release);
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
