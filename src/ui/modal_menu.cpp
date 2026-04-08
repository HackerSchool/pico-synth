#include "Ui.hpp"
#include "pico/time.h"

namespace {
const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                             66, 68, 70, -1, 67, 69, 71, 72};
constexpr int kModalExciterCount = 4;

template <typename T> T constrain(T value, T min_val, T max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}
} // namespace

void UiHandler::modal_edit_handle_switches(UiHandler &self) {
    const uint16_t curr = self.hw.curr_switches;
    KeyChanges changes = compute_key_changes(self.prev_switches, curr);
    update_leds_from_keys(i2c1, self.prev_switches, curr);

    for (int i = 0; i < 16; ++i) {
        if ((changes.note_on_mask >> i) & 1) {
            if (i == 3) {
                self.begin_randomizer_hold();
                continue;
            }

            const uint8_t note = key_to_midi[i];

            if (note != 255) {
                const uint8_t midi_note =
                    static_cast<uint8_t>(note + (12 * self.octave));
                self.modal_last_note = midi_note;
                self.modal_edit_dirty = true;

                if (self.switches_in) {
                    self.track_switch_note_on(i, self.midi_channel, midi_note);
                }
            } else {
                switch (i) {
                case 0:
                    if (self.octave > -5) {
                        self.octave--;
                        self.modal_edit_dirty = true;
                    }
                    break;
                case 11:
                    if (self.octave < 4) {
                        self.octave++;
                        self.modal_edit_dirty = true;
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

            const uint8_t note = key_to_midi[i];
            if (note != 255) {
                self.release_tracked_switch_note(i);
            }
        }
    }

    self.prev_switches = curr;
}

void UiHandler::modal_edit_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;
    Synth &synth = self.synth;
    ModalPatch &patch = synth.modal_patch_storage[self.midi_channel];
    const bool advanced_page = self.ui_state == UI_STATE_MODAL_EDIT;

    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        const int32_t delta = enc->delta;

        if (UiHandler::encoder_moved(delta)) {
            bool param_changed = false;

            if (!advanced_page) {
                switch (i) {
                case 0:
                    patch.structure = static_cast<uint8_t>(constrain(
                        static_cast<int>(patch.structure) +
                            UiHandler::encoder_velocity_delta(delta, 1, 4, 8),
                        0, 127));
                    param_changed = true;
                    break;
                case 1:
                    patch.brightness = static_cast<uint8_t>(constrain(
                        static_cast<int>(patch.brightness) +
                            UiHandler::encoder_velocity_delta(delta, 1, 4, 8),
                        0, 127));
                    param_changed = true;
                    break;
                case 2:
                    patch.damping = static_cast<uint8_t>(constrain(
                        static_cast<int>(patch.damping) +
                            UiHandler::encoder_velocity_delta(delta, 1, 4, 8),
                        0, 127));
                    param_changed = true;
                    break;
                case 3:
                    patch.position = static_cast<uint8_t>(constrain(
                        static_cast<int>(patch.position) +
                            UiHandler::encoder_velocity_delta(delta, 1, 4, 8),
                        0, 127));
                    param_changed = true;
                    break;
                default:
                    break;
                }
            } else {
                switch (i) {
                case 0: {
                    int exciter_index = static_cast<int>(patch.exciter_type);
                    exciter_index +=
                        UiHandler::encoder_velocity_delta(delta, 1, 1, 1);
                    if (exciter_index < 0) exciter_index = kModalExciterCount - 1;
                    if (exciter_index >= kModalExciterCount) exciter_index = 0;
                    patch.exciter_type =
                        static_cast<ModalExciterType>(exciter_index);
                    param_changed = true;
                    break;
                }
                case 1:
                    patch.structure = static_cast<uint8_t>(constrain(
                        static_cast<int>(patch.structure) +
                            UiHandler::encoder_velocity_delta(delta, 1, 4, 8),
                        0, 127));
                    param_changed = true;
                    break;
                case 2:
                    patch.brightness = static_cast<uint8_t>(constrain(
                        static_cast<int>(patch.brightness) +
                            UiHandler::encoder_velocity_delta(delta, 1, 4, 8),
                        0, 127));
                    param_changed = true;
                    break;
                case 3:
                    patch.damping = static_cast<uint8_t>(constrain(
                        static_cast<int>(patch.damping) +
                            UiHandler::encoder_velocity_delta(delta, 1, 4, 8),
                        0, 127));
                    param_changed = true;
                    break;
                default:
                    break;
                }
            }

            if (param_changed) {
                self.mark_modal_patch_updated(self.midi_channel);
                self.modal_edit_dirty = true;
            }
        }

        if (!enc->button_state && enc->button_edge) {
            if (i == 0) {
                self.release_all_tracked_switch_notes();
                self.ui_state = advanced_page ? UI_STATE_MAIN : UI_STATE_MODAL_EDIT;
            } else if (i == 3) {
                self.release_all_tracked_switch_notes();
                self.ui_state = UI_STATE_CHOOSE;
                self.chosen_index = UI_STATE_FM_EDIT;
                self.chosen_dirty = true;
            }
            self.modal_edit_dirty = true;
        }
    }
}

void UiHandler::modal_edit_update_display(UiHandler &self) {
    if (self.preset_browse_overlay_active()) {
        return;
    }

    if (self.modal_edit_dirty) {
        ModalPatch &patch = self.synth.modal_patch_storage[self.midi_channel];
        if (self.ui_state == UI_STATE_MODAL_EDIT) {
            self.hw.draw_modal_edit(self.midi_channel, self.octave,
                                    patch.exciter_type, patch.structure,
                                    patch.brightness, patch.damping,
                                    patch.position, self.modal_last_note);
        } else {
            self.hw.draw_modal_main(self.midi_channel, self.octave,
                                    patch.structure, patch.brightness,
                                    patch.damping, patch.position,
                                    patch.exciter_type, self.modal_last_note);
        }
        self.hw.display_show();
        self.modal_edit_dirty = false;
    }
}
