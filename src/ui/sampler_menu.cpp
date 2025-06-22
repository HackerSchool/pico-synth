#include "HardwareManager.hpp"
#include "Ui.hpp"

void UiHandler::sampler_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;
    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        int32_t delta = enc->delta;

        // Handle encoder rotations (if needed for future features)
        if (delta != 0 && abs(delta) > 1) {
            switch (i) {
            case 0:
                self.sampler_dirty = true;
                break;
            case 1:
                self.wav_files.print_files(); // Optional: print on startup
                
                self.sample_index = (self.sample_index + (delta > 0 ? 1 : -1) + 4) % (4);
                
                /*
                printf("WAV files count: %d\n", self.wav_files.get_count());
                if (self.wav_files.get_count() > 0) {
                    printf("Sample index: %d\n", self.sample_index);
                    printf("Sample: %s\n", self.wav_files.get_filename(self.sample_index).c_str());
                } else {
                    printf("No WAV files found!\n");
                }                
                */
                self.sampler_dirty = true;
                break;
            case 2:
                self.sampler_dirty = true;
                break;
            case 3:
                self.sampler_dirty = true;
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
                self.sampler_dirty = true;
                break;
            case 1:
                self.sampler_dirty = true;
                break;
            case 2:
                self.sampler_dirty = true;
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
        hw.draw_sampler_menu(self.wav_files, self.sample_index, self.sample_channel);
        hw.display_show();
        self.sampler_dirty = false;
    }
}
