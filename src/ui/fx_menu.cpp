#include "HardwareManager.hpp"
#include "Ui.hpp"

void UiHandler::fx_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;
    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        int32_t delta = enc->delta;

        // Handle encoder rotations (if needed for future features)
        if (delta != 0 && abs(delta) > 1) {
            switch (i) {
            case 0:
                // Eventually to select FXs
                break;
            case 1:
                // Encoder 1: Change delay_ms
                self.delay_ms = self.delay_ms + (delta > 0 ? 10 : -10);
                // Clamp delay_ms to reasonable range (e.g., 0-1000ms)
                if (self.delay_ms < 0) self.delay_ms = 0;
                if (self.delay_ms > 1000) self.delay_ms = 1000;
                self.fx_dirty = true;
                break;
            case 2:
                // Encoder 2: Change feedback (0.0 to 1.0)
                self.feedback += (delta > 0 ? 500 : -500);
                // Clamp feedback to 0.0-1.0 range
                if (self.feedback < 0) self.feedback = 0;
                if (self.feedback > 32000) self.feedback = 32000;
                self.fx_dirty = true;
                break;
            case 3:
                // Encoder 3: Change mix (0.0 to 1.0)
                self.mix += (delta > 0 ? 500 : -500);
                // Clamp mix to 0.0-1.0 range
                if (self.mix < 0) self.mix = 0;
                if (self.mix > 32000) self.mix = 32000;
                self.fx_dirty = true;
                break;
            }
            self.set_delay_param(self.delay_ms, self.feedback, self.mix);
        }

        // Handle button presses for toggling flags
        if (!enc->button_state && enc->button_edge) {
            switch (i) {
            case 0:
                //self.midi_out = !self.midi_out;
                //self.midi_settings_dirty = true;
                //printf("MIDI Out: %s\n", self.midi_out ? "ON" : "OFF");
                self.sampler_dirty = true;
                break;
            case 1:
                self.sampler_dirty = true;
                break;
            case 2:
                self.sampler_dirty = true;
                break;
            case 3:
                // Encoder 4 button: Exit back to main menu
                self.ui_state = UI_STATE_CHOOSE;
                self.chosen_index = (UI_STATE_FX_EDIT + 1 + NUM_USABLE_STATES) % NUM_USABLE_STATES;
                self.fx_dirty = true;
                self.chosen_dirty = true;
                printf("State: CHOOSE_STATE\n");
                break;
            }
        }
    }
}

void UiHandler::fx_update_display(UiHandler &self) {
    HardwareManager &hw = self.hw;

    if (self.fx_dirty) {
        hw.draw_fx_menu(self.delay_ms, self.feedback, self.mix);
        hw.display_show();
        self.fx_dirty = false;
    }
}
