#include "Ui.hpp"
#include "draw_utils.hpp"

const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                             66, 68, 70, -1, 67, 69, 71, 72};

// Helper function you'll need to add (if not already present):
template <typename T> T constrain(T value, T min_val, T max_val) {
    if (value < min_val)
        return min_val;
    if (value > max_val)
        return max_val;
    return value;
}

void UiHandler::fm_edit_handle_switches(UiHandler &self) {
    MidiHandler &midi = self.midi;
    Sampler &sampler = self.sampler;
    // Synth &synth = self.synth;

    uint16_t curr = self.hw.curr_switches;
    KeyChanges changes = compute_key_changes(self.prev_switches, curr);
    update_leds_from_keys(i2c1, self.prev_switches, curr);

    for (int i = 0; i < 16; ++i) {
        if ((changes.note_on_mask >> i) & 1) {
            uint8_t note = key_to_midi[i];

            // Switches 1-6: Select operator (switches 1-2 for 2 operators)
            if (i >= 1 && i <= 6) {
                if (i <= 2) { // Only operators 1-2 available
                    self.selected_operator = i - 1; // 0-1
                    self.fm_edit_mode = 0; // Back to operator selection
                    self.fm_edit_dirty = true;
                    printf("Selected operator: %d\n", self.selected_operator);
                }
            }
            // Switch 7: ADSR edit mode
            else if (i == 7) {
                self.fm_edit_mode = 1;
                self.fm_edit_dirty = true;
                printf("ADSR edit mode\n");
            }
            // Switch 8: Parameter edit mode
            else if (i == 8) {
                self.fm_edit_mode = 2;
                self.fm_edit_dirty = true;
                printf("Parameter edit mode\n");
            }
            // Other switches: Play notes
            else if (note != 255) {
                if (self.midi_channel == 5) {
                    sampler.trigger_player(note - 60);
                } else if (self.switches_in) {
                    uint8_t packet[4];
                    packet[0] = 0x09; // CIN = Note On, Cable 0
                    packet[1] = 0x90 | (self.midi_channel & 0x0F);
                    packet[2] = note + 12 * self.octave;
                    packet[3] = 0x7F; // Velocity
                    midi.midi_receive_note(packet);
                }
            }
            // Octave controls
            else {
                switch (i) {
                case 0:
                    if (self.octave > -5) {
                        self.octave--;
                        self.fm_edit_dirty = true;
                    }
                    break;
                case 11:
                    if (self.octave < 4) {
                        self.octave++;
                        self.fm_edit_dirty = true;
                    }
                    break;
                }
            }
        }

        // Note off handling
        if ((changes.note_off_mask >> i) & 1) {
            uint8_t note = key_to_midi[i];
            if (note != 255 &&
                !(i >= 1 &&
                  i <= 8)) { // Don't send note off for control switches
                if (self.switches_in) {
                    uint8_t packet[4];
                    packet[0] = 0x08; // CIN = Note Off, Cable 0
                    packet[1] = 0x80 | (self.midi_channel & 0x0F);
                    packet[2] = note + 12 * self.octave;
                    packet[3] = 0x7F;
                    midi.midi_receive_note(packet);
                }
            }
        }
    }

    self.prev_switches = curr;
}

void UiHandler::fm_edit_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;
    Synth &synth = self.synth;

    Patch &patch = synth.patch_storage[self.midi_channel];
    OperatorParams &op = patch.ops[self.selected_operator];

    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        int32_t delta = enc->delta;

        if (delta != 0 && abs(delta) > 1) {
            bool param_changed = false;

            switch (self.fm_edit_mode) {
            case 1: // ADSR mode
                switch (i) {
                case 0: // Attack
                    op.attack =
                        constrain(op.attack + (delta > 0 ? 1 : -1), 0, 127);
                    param_changed = true;
                    break;
                case 1: // Decay
                    op.decay =
                        constrain(op.decay + (delta > 0 ? 1 : -1), 0, 127);
                    param_changed = true;
                    break;
                case 2: // Sustain
                    op.sustain =
                        constrain(op.sustain + (delta > 0 ? 1 : -1), 0, 127);
                    param_changed = true;
                    break;
                case 3: // Release
                    op.release =
                        constrain(op.release + (delta > 0 ? 1 : -1), 0, 127);
                    param_changed = true;
                    break;
                }
                break;

            case 2: // Parameter mode
                switch (i) {
                case 0: // Ratio
                    op.ratio =
                        constrain(op.ratio + (delta > 0 ? 1 : -1), 1, 16);
                    param_changed = true;
                    break;
                case 1: // Feedback
                    op.feedback =
                        constrain(op.feedback + (delta > 0 ? 1 : -1), 0, 127);
                    param_changed = true;
                    break;
                case 2: // FM Depth
                    op.fm_depth =
                        constrain(op.fm_depth + (delta > 0 ? 1 : -1), 0, 127);
                    param_changed = true;
                    break;
                case 3: // Wave Type
                {
                    int wave_val = static_cast<int>(op.wave_type);
                    wave_val += (delta > 0 ? 1 : -1);
                    if (wave_val < 0)
                        wave_val = 3; // Wrap to last wave type
                    if (wave_val > 3)
                        wave_val = 0; // Wrap to first wave type
                    op.wave_type = static_cast<WaveType>(wave_val);
                    param_changed = true;
                } break;
                }
                break;
            }

            if (param_changed) {
                // Update the active patch atomically
                synth.active_patch[self.midi_channel].store(
                    &patch, std::memory_order_release);
                synth.patch_dirty_flags.set(self.midi_channel);
                self.fm_edit_dirty = true;
            }
        }

        // Button presses
        if (!enc->button_state && enc->button_edge) {
            switch (i) {
            case 0: // Encoder 1 button - cycle through operators
                self.selected_operator =
                    (self.selected_operator + 1) % OP_PER_VOICE;
                self.fm_edit_dirty = true;
                break;
            case 1: // Encoder 2 button - toggle ADSR mode
                self.fm_edit_mode = (self.fm_edit_mode == 1) ? 0 : 1;
                self.fm_edit_dirty = true;
                break;
            case 2: // Encoder 3 button - toggle parameter mode
                self.fm_edit_mode = (self.fm_edit_mode == 2) ? 0 : 2;
                self.fm_edit_dirty = true;
                break;
            case 3: // Encoder 4 button - back to previous menu
                self.ui_state =
                    UI_STATE_MAIN; // or whatever your previous state was
                self.main_dirty = true;
                break;
            }
        }
    }
}

void UiHandler::fm_edit_update_display(UiHandler &self) {
    HardwareManager &hw = self.hw;
    Synth &synth = self.synth;
    bool changed = false;

    if (self.fm_edit_dirty) {
        Patch &patch = synth.patch_storage[self.midi_channel];
        OperatorParams &op = patch.ops[self.selected_operator];

        hw.draw_fm_edit(self.midi_channel, self.selected_operator, self.octave,
                        self.fm_edit_mode, op.wave_type, op.attack, op.decay,
                        op.sustain, op.release, op.ratio, op.feedback,
                        op.fm_depth);

        changed = true;
        self.fm_edit_dirty = false;
    }

    if (changed) {
        hw.display_show();
    }
}

// // Add this function to HardwareManager class:
// void HardwareManager::draw_string_inverted(int x, int y, const char* text) {
//     ssd1306_draw_string_inverted(&disp, x, y, 1, text);
// }
//
// void HardwareManager::draw_string(int x, int y, const char* text) {
//     ssd1306_draw_string(&disp, x, y, 1, text);
// }
