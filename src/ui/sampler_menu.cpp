#include "HardwareManager.hpp"
#include "Ui.hpp"

/*
self.chosen_index = 0 -> Main Menu
self.chosen_index = 1 -> MIDI
self.chosen_index = 2 -> Sequencer
*/

void UiHandler::sampler_handle_encoders(UiHandler &self) {
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
                break;
            case 2:
                self.chosen_dirty = true;
                break;
            case 3:
                // Exit back to main menu
                self.ui_state = UI_STATE_CHOOSE;
                self.chosen_index = (UI_STATE_SAMPLER + 1 + NUM_USABLE_STATES) % NUM_USABLE_STATES;
                self.sampler_dirty = true;
                self.chosen_dirty = true;
                printf("State: CHOOSE_STATE\n");
                break;
            }
        }
    }
}

void UiHandler::sampler_update_display(UiHandler &self) {
    HardwareManager &hw = self.hw;

    if (self.sampler_dirty) {
        hw.draw_sampler_menu(self.sample_name);
        hw.display_show();
        self.sampler_dirty = false;
    }
}
