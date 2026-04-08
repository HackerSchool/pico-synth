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
            if (i == 3) {
                self.end_randomizer_hold();
                continue;
            }

            uint8_t note = key_to_midi[i];
            if (note != 255) {
                self.release_tracked_switch_note(i);
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

        if (UiHandler::encoder_moved(delta)) {
            bool param_changed = false;

            switch (self.fm_edit_mode) {
            case 0: // Select/main parameter view
                switch (i) {
                case 0: // Wave type
                {
                    int wave_val = static_cast<int>(op.wave_type);
                    wave_val += UiHandler::encoder_velocity_delta(delta, 1, 1, 1);
                    if (wave_val < 0)
                        wave_val = static_cast<int>(WaveType::Sinc);
                    if (wave_val > static_cast<int>(WaveType::Sinc))
                        wave_val = 0;
                    op.wave_type = static_cast<WaveType>(wave_val);
                    param_changed = true;
                } break;
                case 1: // Ratio
                    op.ratio = constrain(
                        op.ratio + UiHandler::encoder_velocity_delta(delta, 1, 2, 4), 1, 16);
                    param_changed = true;
                    break;
                case 2: // Feedback
                    op.feedback = constrain(
                        op.feedback + UiHandler::encoder_velocity_delta(delta, 1, 4, 8), 0, 127);
                    param_changed = true;
                    break;
                case 3: // FM depth
                    op.fm_depth = constrain(
                        op.fm_depth + UiHandler::encoder_velocity_delta(delta, 1, 4, 8), 0, 127);
                    param_changed = true;
                    break;
                }
                break;
            case 1: // ADSR mode
                switch (i) {
                case 0: // Attack
                    op.attack = constrain(
                        op.attack + UiHandler::encoder_velocity_delta(delta, 1, 4, 8), 0, 127);
                    param_changed = true;
                    break;
                case 1: // Decay
                    op.decay = constrain(
                        op.decay + UiHandler::encoder_velocity_delta(delta, 1, 4, 8), 0, 127);
                    param_changed = true;
                    break;
                case 2: // Sustain
                    op.sustain = constrain(
                        op.sustain + UiHandler::encoder_velocity_delta(delta, 1, 4, 8), 0, 127);
                    param_changed = true;
                    break;
                case 3: // Release
                    op.release = constrain(
                        op.release + UiHandler::encoder_velocity_delta(delta, 1, 4, 8), 0, 127);
                    param_changed = true;
                    break;
                }
                break;

            case 2: // Parameter mode
                switch (i) {
                case 0: // Ratio
                    op.ratio = constrain(
                        op.ratio + UiHandler::encoder_velocity_delta(delta, 1, 2, 4), 1, 16);
                    param_changed = true;
                    break;
                case 1: // Feedback
                    op.feedback = constrain(
                        op.feedback + UiHandler::encoder_velocity_delta(delta, 1, 4, 8), 0, 127);
                    param_changed = true;
                    break;
                case 2: // FM Depth
                    op.fm_depth = constrain(
                        op.fm_depth + UiHandler::encoder_velocity_delta(delta, 1, 4, 8), 0, 127);
                    param_changed = true;
                    break;
                case 3: // Wave Type
                {
                    int wave_val = static_cast<int>(op.wave_type);
                    wave_val += UiHandler::encoder_velocity_delta(delta, 1, 1, 1);
                    if (wave_val < 0)
                        wave_val = static_cast<int>(WaveType::Sinc);
                    if (wave_val > static_cast<int>(WaveType::Sinc))
                        wave_val = 0; // Wrap to first wave type
                    op.wave_type = static_cast<WaveType>(wave_val);
                    param_changed = true;
                } break;
                }
                break;
            }

            if (param_changed) {
                self.mark_fm_patch_updated(self.midi_channel);
                self.fm_edit_dirty = true;
            }
        }

        // Button presses
        if (!enc->button_state && enc->button_edge) {
            switch (i) {
            case 0: // Encoder 1 button - back to FM engine view
                self.release_all_tracked_switch_notes();
                self.ui_state = UI_STATE_MAIN;
                self.main_dirty = true;
                self.channel_dirty = true;
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
            case 3: // Encoder 4 button - select next operator
                self.selected_operator =
                    (self.selected_operator + 1) % OP_PER_VOICE;
                self.fm_edit_dirty = true;
                break;
            }
        }
    }
}

void UiHandler::fm_edit_update_display(UiHandler &self) {
    if (self.preset_browse_overlay_active()) {
        return;
    }

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
