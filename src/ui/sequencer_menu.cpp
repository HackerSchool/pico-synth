#include "HardwareManager.hpp"
#include "Sequencer.hpp"
#include "Ui.hpp"

void UiHandler::sequencer_handle_encoders(UiHandler &self) {

    HardwareManager &hw = self.hw;
    Sequencer &sequencer = self.seq;
    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        int32_t delta = enc->delta;

        // Handle encoder rotations
        if (UiHandler::encoder_moved(delta)) {
            switch (i) {
            case 0:
                // Tempo control (encoder 0)
                {
                    int tempo = static_cast<int>(self.display_tempo) +
                                UiHandler::encoder_velocity_delta(delta, 1, 5, 20);
                    if (tempo < 5) tempo = 5;
                    if (tempo > static_cast<int>(self.max_tempo)) {
                        tempo = static_cast<int>(self.max_tempo);
                    }
                    self.display_tempo = static_cast<uint32_t>(tempo);
                }
                sequencer.set_tempo(self.display_tempo);
                self.sequencer_settings_dirty = true;
                printf("Tempo: %ld BPM\n", self.display_tempo);
                break;
            case 1:
                // Could be used for swing or other parameters in the future
                break;
            case 2:
                // Could be used for pattern length or other parameters
                break;
            }
        }

        // Handle button presses
        if (!enc->button_state && enc->button_edge) {
            switch (i) {
            case 0:
                // Play/Pause toggle (encoder 0 button)
                self.sequencer_playing = !self.sequencer_playing;
                if (self.sequencer_playing) {
                    sequencer.play();
                    printf("Sequencer: PLAY\n");
                } else {
                    sequencer.pause();
                    printf("Sequencer: PAUSE\n");
                }
                self.sequencer_settings_dirty = true;
                break;
            case 1:
                // Reset to step 1 (encoder 1 button)
                sequencer.play_from_step(0);
                self.display_current_step = 0;
                self.sequencer_settings_dirty = true;
                printf("Sequencer: Reset to step 1\n");
                break;
            case 2:
                self.ui_state = UI_STATE_SEQUENCER_EDIT;
                self.sequencer_dirty = true;
                printf("State: STATE_SEQUENCER_EDIT\n");
                break;
            case 3:
                // Exit back to main menu
                self.ui_state = UI_STATE_CHOOSE;
                self.chosen_index = UI_STATE_SEQUENCER;
                printf("State: CHOOSE_STATE\n");
                self.chosen_dirty = true;
                self.channel_dirty = true;
                self.filter_dirty = true;
                self.adsr_dirty = true;
                self.main_dirty = true;
                break;
            }
        }
    }
}

void UiHandler::sequencer_update_display(UiHandler &self) {
    if (self.preset_browse_overlay_active()) {
        return;
    }

    Sequencer &sequencer = self.seq;
    HardwareManager &hw = self.hw;

    // Check if step has changed
    uint8_t current_step = sequencer.get_current_step();
    bool step_changed = (current_step != self.display_current_step);
    self.display_current_step = current_step;

    // Update display if settings changed OR step changed
    if (self.sequencer_settings_dirty || step_changed) {
        hw.draw_sequencer_settings(self.sequencer_playing, self.display_tempo,
                                   self.display_current_step);
        hw.display_show();
        self.sequencer_settings_dirty = false;
    }
}
