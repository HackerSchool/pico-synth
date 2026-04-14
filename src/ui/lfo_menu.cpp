#include "HardwareManager.hpp"
#include "Ui.hpp"

namespace {
const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                             66, 68, 70, -1, 67, 69, 71, 72};

int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

const char *engine_name(SynthEngine engine) {
    switch (engine) {
    case SynthEngine::KarplusStrong:
        return "KARPLUS";
    case SynthEngine::Modal:
        return "MODAL";
    case SynthEngine::FM:
    default:
        return "FM";
    }
}
} // namespace

void UiHandler::lfo_handle_switches(UiHandler &self) {
    const uint16_t curr = self.hw.curr_switches;
    const KeyChanges changes = compute_key_changes(self.prev_switches, curr);
    update_leds_from_keys(i2c1, self.prev_switches, curr);

    for (int i = 0; i < 16; ++i) {
        if ((changes.note_on_mask >> i) & 1) {
            if (i == 3) {
                self.begin_randomizer_hold();
                continue;
            }

            const uint8_t note = key_to_midi[i];
            if (note != 255) {
                if (self.switches_in) {
                    const uint8_t midi_note =
                        static_cast<uint8_t>(note + (12 * self.octave));
                    self.track_switch_note_on(i, self.midi_channel, midi_note);
                }
            } else {
                switch (i) {
                case 0:
                    if (self.octave > -5) {
                        self.octave--;
                        self.lfo_dirty = true;
                    }
                    break;
                case 11:
                    if (self.octave < 4) {
                        self.octave++;
                        self.lfo_dirty = true;
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

void UiHandler::lfo_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;
    LfoSettings &lfo = self.lfo_settings[self.selected_lfo_index];
    const SynthEngine engine = self.synth.get_engine();

    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        const int32_t delta = enc->delta;

        if (UiHandler::encoder_moved(delta)) {
            switch (i) {
            case 0: {
                const int dir =
                    UiHandler::encoder_velocity_delta(delta, 1, 1, 1);
                const int route_count = UiHandler::lfo_route_count(engine);
                int route_index =
                    UiHandler::lfo_target_index_for_engine(engine, lfo.target) + dir;
                route_index = clamp_int(route_index, 0, route_count - 1);
                lfo.target =
                    UiHandler::lfo_target_from_engine_index(engine, route_index);
                self.lfo_dirty = true;
                break;
            }
            case 1: {
                int wave_index = static_cast<int>(lfo.wave_type);
                wave_index += UiHandler::encoder_velocity_delta(delta, 1, 1, 1);
                if (wave_index < 0) {
                    wave_index = static_cast<int>(WaveType::Sinc);
                } else if (wave_index > static_cast<int>(WaveType::Sinc)) {
                    wave_index = 0;
                }
                lfo.wave_type = static_cast<WaveType>(wave_index);
                self.lfo_dirty = true;
                break;
            }
            case 2: {
                const int step_delta = UiHandler::analog_encoder_delta(
                    delta, lfo.frequency_hundredths_hz);
                lfo.frequency_hundredths_hz = static_cast<uint16_t>(clamp_int(
                    static_cast<int>(lfo.frequency_hundredths_hz) + step_delta,
                    0, 2000));
                self.lfo_dirty = true;
                break;
            }
            case 3:
                lfo.depth_hundredths_percent = static_cast<uint16_t>(clamp_int(
                    static_cast<int>(lfo.depth_hundredths_percent) +
                        UiHandler::analog_encoder_delta(
                            delta, lfo.depth_hundredths_percent),
                    0, 10000));
                self.lfo_dirty = true;
                break;
            default:
                break;
            }
        }

        if (!enc->button_state && enc->button_edge) {
            switch (i) {
            case 0:
                self.selected_lfo_index =
                    static_cast<uint8_t>((self.selected_lfo_index + 1) % LFO_COUNT);
                self.lfo_dirty = true;
                break;
            case 3:
                self.ui_state = UI_STATE_CHOOSE;
                self.chosen_index = UI_STATE_LFO;
                self.chosen_dirty = true;
                self.lfo_dirty = true;
                break;
            default:
                self.lfo_dirty = true;
                break;
            }
        }
    }
}

void UiHandler::lfo_update_display(UiHandler &self) {
    if (self.preset_browse_overlay_active()) {
        return;
    }

    if (self.lfo_dirty) {
        const LfoSettings &lfo = self.lfo_settings[self.selected_lfo_index];
        const SynthEngine engine = self.synth.get_engine();
        const LfoTarget display_target =
            UiHandler::lfo_target_matches_engine(lfo.target, engine)
                ? lfo.target
                : LfoTarget::Off;
        self.hw.draw_lfo_menu(self.selected_lfo_index,
                              static_cast<uint8_t>(LFO_COUNT),
                              engine_name(engine),
                              UiHandler::lfo_target_to_string(display_target),
                              lfo.wave_type, lfo.frequency_hundredths_hz,
                              lfo.depth_hundredths_percent);
        self.hw.display_show();
        self.lfo_dirty = false;
    }
}
