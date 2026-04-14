#include "HardwareManager.hpp"
#include "Ui.hpp"

namespace {
inline int clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}
} // namespace

void UiHandler::fx_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;
    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        int32_t delta = enc->delta;

        if (UiHandler::encoder_moved(delta)) {
            bool param_changed = false;

            switch (i) {
            case 0:
                if (delta > 0) {
                    self.current_fx = (self.current_fx + 1) % UiHandler::FX_COUNT;
                } else {
                    self.current_fx =
                        (self.current_fx - 1 + UiHandler::FX_COUNT) % UiHandler::FX_COUNT;
                }
                self.fx_dirty = true;
                break;
            case 1:
                // Use encoder delta magnitude as a lightweight speed estimate:
                // small turns get fine resolution, faster turns jump further.
                self.fx_params[self.current_fx].p1 = clamp_int(
                    self.fx_params[self.current_fx].p1 +
                        UiHandler::encoder_velocity_delta(delta, 10, 100, 500),
                    0,
                    1000);
                self.fx_dirty = true;
                param_changed = true;
                break;
            case 2:
                self.fx_params[self.current_fx].p2 = clamp_int(
                    self.fx_params[self.current_fx].p2 +
                        UiHandler::encoder_velocity_delta(delta, 50, 200, 500),
                    0,
                    32000);
                self.fx_dirty = true;
                param_changed = true;
                break;
            case 3:
                self.fx_params[self.current_fx].mix = clamp_int(
                    self.fx_params[self.current_fx].mix +
                        UiHandler::encoder_velocity_delta(delta, 50, 200, 500),
                    0,
                    32000);
                self.fx_dirty = true;
                param_changed = true;
                break;
            default:
                break;
            }

            if (param_changed) {
                self.mark_fx_params_updated(self.current_fx);
            }
        }

        if (!enc->button_state && enc->button_edge) {
            switch (i) {
            case 0: {
                bool enabled = !self.fx_enabled[self.current_fx];
                self.set_fx_enabled(self.current_fx, enabled);
                self.fx_dirty = true;
                break;
            }
            case 1:
                self.sampler_dirty = true;
                break;
            case 2:
                self.sampler_dirty = true;
                break;
            case 3:
                self.ui_state = UI_STATE_CHOOSE;
                self.chosen_index = UI_STATE_FX_EDIT;
                self.fx_dirty = true;
                self.chosen_dirty = true;
                break;
            default:
                break;
            }
        }
    }
}

void UiHandler::fx_update_display(UiHandler &self) {
    if (self.preset_browse_overlay_active()) {
        return;
    }

    HardwareManager &hw = self.hw;

    if (self.fx_dirty) {
        const FxParams &params = self.fx_params[self.current_fx];
        hw.draw_fx_menu(
            self.current_fx, self.fx_enabled[self.current_fx], params.p1, params.p2, params.mix);
        hw.display_show();
        self.fx_dirty = false;
    }
}
