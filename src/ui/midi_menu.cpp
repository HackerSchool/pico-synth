#include "HardwareManager.hpp"
#include "Ui.hpp"

void UiHandler::midi_handle_encoders(UiHandler &self) {
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
            }
        }

        // Handle button presses for toggling flags
        if (!enc->button_state && enc->button_edge) {
            switch (i) {
            case 0:
                self.midi_out = !self.midi_out;
                self.midi_settings_dirty = true;
                printf("MIDI Out: %s\n", self.midi_out ? "ON" : "OFF");
                break;
            case 1:
                self.midi_in = !self.midi_in;
                self.midi_settings_dirty = true;
                printf("MIDI In: %s\n", self.midi_in ? "ON" : "OFF");
                break;
            case 2:
                self.switches_in = !self.switches_in;
                self.midi_settings_dirty = true;
                printf("Switches In: %s\n", self.switches_in ? "ON" : "OFF");
                break;
            case 3:
                // Exit back to main menu
                self.ui_state = UI_STATE_CHOOSE;
                self.chosen_index = (UI_STATE_MIDI_SETTINGS + 1 + NUM_USABLE_STATES) % NUM_USABLE_STATES;
                self.midi_settings_dirty = true;
                self.chosen_dirty = true;
                printf("State: CHOOSE_STATE\n");
                break;
            }
        }
    }
}

void UiHandler::midi_update_display(UiHandler &self) {
    HardwareManager &hw = self.hw;

    if (self.midi_settings_dirty) {
        hw.draw_midi_settings(self.midi_out, self.midi_in, self.switches_in,
                              self.sequencer_in, self.sequencer_out);
        hw.display_show();
        self.midi_settings_dirty = false;
    }
}
