#include "HardwareManager.hpp"
#include "Ui.hpp"

void UiHandler::choose_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;
    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        int32_t delta = enc->delta;

        // Handle encoder rotations (if needed for future features)
        if (delta != 0 && abs(delta) > 1) {
            switch (i) {
            case 0:
                // Could cycle through MIDI channels or other settings
                break;
            case 1:
                // Could adjust MIDI velocity or other parameters
                break;
            case 2:
                // Could adjust other MIDI settings
                break;
            case 3:
                if (delta > 0) {
                    self.chosen_index += 1;
                } else {
                    self.chosen_index -= 1;
                }
                self.chosen_dirty = true;
                break;
            }
        }

        // Handle button presses for toggling flags
        if (!enc->button_state && enc->button_edge) {
            switch (i) {
            case 0:
                //self.midi_out = !self.midi_out;
                //self.midi_settings_dirty = true;
                //printf("MIDI Out: %s\n", self.midi_out ? "ON" : "OFF");
                self.chosen_dirty = true;
                break;
            case 1:
                self.chosen_dirty = true;
                break
            case 2:
                self.chosen_dirty = true;
                break;
            case 3:
                self.chosen_dirty = true;
                break;
            }
        }
    }
}

void UiHandler::choose_update_display(UiHandler &self) {
    HardwareManager &hw = self.hw;

    if (self.choose_dirty) {
        hw.draw_choose_menu(self.chosen_index);
        hw.display_show();
        self.choose_dirty = false;
    }
}
