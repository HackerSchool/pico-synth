#include "HardwareManager.hpp"
#include "Ui.hpp"

namespace {
int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}
} // namespace

void UiHandler::analog_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;

    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        const int32_t delta = enc->delta;

        if (UiHandler::encoder_moved(delta)) {
            switch (i) {
            case 1: {
                const int step_delta = UiHandler::analog_encoder_delta(
                    delta, self.analog_settings.frequency_hundredths_hz);
                const int next_value = clamp_int(
                    static_cast<int>(self.analog_settings.frequency_hundredths_hz) +
                        step_delta,
                    0, 40000); // 0.00 Hz to 400.00 Hz
                self.analog_settings.frequency_hundredths_hz =
                    static_cast<uint16_t>(next_value);
                self.analog_offsets_initialized = false;
                self.analog_reapply_pending = true;
                self.analog_dirty = true;
                break;
            }
            case 2: {
                const int step_delta = UiHandler::analog_encoder_delta(
                    delta, self.analog_settings.dispersion_hundredths_percent);
                const int next_value = clamp_int(
                    static_cast<int>(self.analog_settings.dispersion_hundredths_percent) +
                        step_delta,
                    0, 10000); // 0.00% to 100.00%
                self.analog_settings.dispersion_hundredths_percent =
                    static_cast<uint16_t>(next_value);
                self.analog_offsets_initialized = false;
                self.analog_reapply_pending = true;
                self.analog_dirty = true;
                break;
            }
            default:
                break;
            }
        }

        if (!enc->button_state && enc->button_edge) {
            switch (i) {
            case 0:
                self.analog_settings.enabled = !self.analog_settings.enabled;
                self.analog_offsets_initialized = false;
                self.analog_reapply_pending = true;
                self.analog_dirty = true;
                break;
            case 3:
                self.ui_state = UI_STATE_CHOOSE;
                self.chosen_index = UI_STATE_ANALOG;
                self.analog_dirty = true;
                self.chosen_dirty = true;
                break;
            default:
                self.analog_dirty = true;
                break;
            }
        }
    }
}

void UiHandler::analog_update_display(UiHandler &self) {
    if (self.preset_browse_overlay_active()) {
        return;
    }

    if (self.analog_dirty) {
        self.hw.draw_analog_menu(self.analog_settings.enabled,
                                 self.analog_settings.frequency_hundredths_hz,
                                 self.analog_settings.dispersion_hundredths_percent);
        self.hw.display_show();
        self.analog_dirty = false;
    }
}
