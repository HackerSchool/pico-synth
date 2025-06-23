#include "HardwareManager.hpp"
#include "Ui.hpp"

/*
self.chosen_index = 0 -> Main Menu
self.chosen_index = 1 -> FM Edit
self.chosen_index = 3 -> MIDI
self.chosen_index = 4 -> Sequencer
*/

static int32_t encoder_accumulator = 0;
const int detent_threshold = 4;  // Change based on your encoder resolution

void UiHandler::choose_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;
    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        int32_t delta = enc->delta;

        // Handle encoder rotations (if needed for future features)
        if (delta != 0 && abs(delta) > 1) {
            printf("D: %ld", delta);
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
                encoder_accumulator += delta;

                if (abs(encoder_accumulator) >= detent_threshold) {
                    int dir = encoder_accumulator > 0 ? 1 : -1;
                    encoder_accumulator = 0; // Reset after handling

                    self.chosen_index += dir;
                    if (self.chosen_index >= NUM_USABLE_STATES - 1) {
                        self.chosen_index = NUM_USABLE_STATES - 1;
                    } else if (self.chosen_index <= 0) {
                        self.chosen_index = 0;
                    }
                    self.chosen_dirty = true;
                }
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
                self.ui_state = static_cast<UiState>(self.chosen_index);
                self.chosen_dirty = true;
                self.main_dirty = true;
                self.adsr_dirty = true;
                self.channel_dirty = true;
                self.filter_dirty = true;
                self.midi_settings_dirty = true;
                self.sequencer_settings_dirty = true;
                self.sequencer_dirty = true;
                self.sampler_dirty = true;
                self.fm_edit_dirty = true;
                self.fx_dirty = true;
                break;
            }
        }
    }
}

void UiHandler::choose_update_display(UiHandler &self) {
    HardwareManager &hw = self.hw;

    if (self.chosen_dirty) {
        hw.draw_choose_menu(self.chosen_index);
        hw.display_show();
        self.chosen_dirty = false;
    }
}
