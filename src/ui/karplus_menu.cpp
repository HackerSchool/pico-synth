#include "Ui.hpp"
#include "draw_utils.hpp"
#include "pico/time.h"

namespace {
const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                             66, 68, 70, -1, 67, 69, 71, 72};
constexpr int kKarplusImpulseCount = 7;

template <typename T> T constrain(T value, T min_val, T max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}
} // namespace

void UiHandler::karplus_edit_handle_switches(UiHandler &self) {
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
                self.karplus_last_note = midi_note;
                self.karplus_last_delay_samples =
                    KarplusVoice::tuned_delay_samples_for_note(midi_note);
                self.karplus_edit_dirty = true;

                if (self.switches_in) {
                    self.track_switch_note_on(i, self.midi_channel, midi_note);
                }
            } else {
                switch (i) {
                case 0:
                    if (self.octave > -5) {
                        self.octave--;
                        self.karplus_edit_dirty = true;
                    }
                    break;
                case 11:
                    if (self.octave < 4) {
                        self.octave++;
                        self.karplus_edit_dirty = true;
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

void UiHandler::karplus_edit_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;
    Synth &synth = self.synth;
    KarplusPatch &patch = synth.karplus_patch_storage[self.midi_channel];
    const bool advanced_page = self.ui_state == UI_STATE_KARPLUS_EDIT;

    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        const int32_t delta = enc->delta;

        if (UiHandler::encoder_moved(delta)) {
            bool param_changed = false;

            if (!advanced_page) {
                switch (i) {
                case 0: {
                    int impulse_index = static_cast<int>(patch.impulse_type);
                    impulse_index +=
                        UiHandler::encoder_velocity_delta(delta, 1, 1, 1);
                    if (impulse_index < 0) impulse_index = kKarplusImpulseCount - 1;
                    if (impulse_index >= kKarplusImpulseCount) impulse_index = 0;
                    patch.impulse_type =
                        static_cast<KarplusImpulseType>(impulse_index);
                    param_changed = true;
                    break;
                }
                case 1:
                    patch.filter_gain = static_cast<uint8_t>(constrain(
                        static_cast<int>(patch.filter_gain) +
                            UiHandler::encoder_velocity_delta(delta, 1, 4, 8),
                        0, 127));
                    param_changed = true;
                    break;
                case 2:
                    patch.decay = static_cast<uint8_t>(constrain(
                        static_cast<int>(patch.decay) +
                            UiHandler::encoder_velocity_delta(delta, 1, 4, 8),
                        0, 127));
                    param_changed = true;
                    break;
                case 3:
                    patch.body_resonance = static_cast<uint8_t>(constrain(
                        static_cast<int>(patch.body_resonance) +
                            UiHandler::encoder_velocity_delta(delta, 1, 4, 8),
                        0, 127));
                    param_changed = true;
                    break;
                default:
                    break;
                }
            } else {
                switch (i) {
                case 0:
                    patch.impulse_length = static_cast<uint8_t>(constrain(
                        static_cast<int>(patch.impulse_length) +
                            UiHandler::encoder_velocity_delta(delta, 1, 4, 8),
                        0, 127));
                    param_changed = true;
                    break;
                case 1:
                    patch.pick_position = static_cast<uint8_t>(constrain(
                        static_cast<int>(patch.pick_position) +
                            UiHandler::encoder_velocity_delta(delta, 1, 4, 8),
                        0, 127));
                    param_changed = true;
                    break;
                case 2:
                    patch.dispersion = static_cast<uint8_t>(constrain(
                        static_cast<int>(patch.dispersion) +
                            UiHandler::encoder_velocity_delta(delta, 1, 4, 8),
                        0, 127));
                    param_changed = true;
                    break;
                case 3:
                    patch.body_resonance = static_cast<uint8_t>(constrain(
                        static_cast<int>(patch.body_resonance) +
                            UiHandler::encoder_velocity_delta(delta, 1, 4, 8),
                        0, 127));
                    param_changed = true;
                    break;
                default:
                    break;
                }
            }

            if (param_changed) {
                self.mark_karplus_patch_updated(self.midi_channel);
                self.karplus_edit_dirty = true;
            }
        }

        if (!enc->button_state && enc->button_edge) {
            if (i == 0) {
                self.release_all_tracked_switch_notes();
                self.ui_state = advanced_page ? UI_STATE_MAIN : UI_STATE_KARPLUS_EDIT;
            } else if (i == 3) {
                self.release_all_tracked_switch_notes();
                self.ui_state = UI_STATE_CHOOSE;
                self.chosen_index = UI_STATE_FM_EDIT;
                self.chosen_dirty = true;
            }
            self.karplus_edit_dirty = true;
        }
    }
}

void UiHandler::karplus_edit_update_display(UiHandler &self) {
    if (self.preset_browse_overlay_active()) {
        return;
    }

    if (self.ui_state != UI_STATE_KARPLUS_EDIT) {
        const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
        if (now_ms - self.waveform_animation_last_ms >= 120) {
            self.waveform_animation_last_ms = now_ms;
            self.waveform_animation_phase =
                static_cast<uint16_t>((self.waveform_animation_phase + 24) %
                                      WAVE_TABLE_LEN);
            self.karplus_edit_dirty = true;
        }
    }

    if (self.karplus_edit_dirty) {
        KarplusPatch &patch = self.synth.karplus_patch_storage[self.midi_channel];
        if (self.ui_state == UI_STATE_KARPLUS_EDIT) {
            self.hw.draw_karplus_edit(self.midi_channel, self.octave,
                                      patch.impulse_length,
                                      patch.pick_position, patch.dispersion,
                                      patch.body_resonance,
                                      self.karplus_last_note,
                                      self.karplus_last_delay_samples);
        } else {
            self.hw.draw_karplus_main(self.midi_channel, self.octave,
                                      patch.impulse_type, patch.filter_gain,
                                      patch.decay, patch.body_resonance,
                                      self.karplus_last_note,
                                      self.karplus_last_delay_samples,
                                      self.waveform_animation_phase);
        }
        self.hw.display_show();
        self.karplus_edit_dirty = false;
    }
}
