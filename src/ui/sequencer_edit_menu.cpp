#include "HardwareManager.hpp"
#include "Sequencer.hpp"
#include "Ui.hpp"

#define MAX_STEPS 16

const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                             66, 68, 70, -1, 67, 69, 71, 72};

void UiHandler::sequencer_note_edit_handle_switches(UiHandler &self) {
    Sequencer &sequencer = self.seq;
    MidiHandler &midi = self.midi;
    uint16_t curr = self.hw.curr_switches;
    KeyChanges changes = compute_key_changes(self.prev_switches, curr);
    update_leds_from_keys(i2c1, self.prev_switches, curr);

    // Light up current step LED (assuming switch 0 and 11 are step controls)
    // You may need to adjust this based on your LED setup

    for (int i = 0; i < 16; ++i) {
        if ((changes.note_on_mask >> i) & 1) {
            uint8_t note = key_to_midi[i];
            if (note != 255) {
                // Piano keys: toggle note on current step
                sequencer.toggle_note_step(self.current_sequencer_step,
                                           note + 12 * self.octave,
                                           self.midi_channel);
                self.sequencer_dirty = true;

                // Auto-stepping: move to next step if enabled
                if (self.auto_stepping_enabled) {
                    self.current_sequencer_step =
                        (self.current_sequencer_step + 1) % MAX_STEPS;
                    self.sequencer_dirty = true;
                }

                // play note
                if (self.switches_in) {
                    uint8_t packet[4];
                    packet[0] = 0x09; // CIN = Note On, Cable 0
                    packet[1] = 0x90 | (self.midi_channel & 0x0F); // Status
                    packet[2] = note + 12 * self.octave;
                    packet[3] = 0x7F; // Velocity
                    midi.midi_receive_note(packet);
                }
                // if (self.midi_out)
                //     midi.midi_send_note(note, 127, true);

            } else {
                // Non-piano keys for step navigation
                // Check for combination presses (switch 3 held)
                bool switch_3_held = (curr >> 3) & 1;

                switch (i) {
                case 0: // Previous step (Switch 3 + 0)
                    if (switch_3_held) {
                        if (self.current_sequencer_step > 0) {
                            self.current_sequencer_step--;
                        } else {
                            self.current_sequencer_step =
                                MAX_STEPS - 1; // Wrap around
                        }
                        self.sequencer_dirty = true;
                        printf("Previous step: %d\n",
                               self.current_sequencer_step);
                    }

                    else if (self.octave > -5) {
                        self.octave--;
                        self.channel_dirty = true;
                    }
                    break;
                case 11: // Next step (Switch 3 + 11)
                    if (switch_3_held) {
                        self.current_sequencer_step =
                            (self.current_sequencer_step + 1) % MAX_STEPS;
                        self.sequencer_dirty = true;
                        printf("Next step: %d\n", self.current_sequencer_step);
                    } else if (self.octave < 4) {
                        self.octave++;
                        self.channel_dirty = true;
                    }
                    break;
                case 3: // Switch 3 - used as modifier, no action on press
                    break;
                default:
                    break;
                }
            }
        }
        if ((changes.note_off_mask >> i) & 1) {
            uint8_t note = key_to_midi[i];
            if (note != 255) {
                if (self.switches_in) {
                    uint8_t packet[4];
                    packet[0] = 0x08; // CIN = Note Off, Cable 0
                    packet[1] = 0x80 | (self.midi_channel & 0x0F);
                    packet[2] = note + 12 * self.octave;
                    packet[3] = 0x7F; // Velocity
                    midi.midi_receive_note(packet);
                }
                // if (self.midi_out)
                //     midi.midi_send_note(note, 0, false);
            }
        }
    }

    self.prev_switches = curr;
}

void UiHandler::sequencer_note_edit_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;
    Sequencer &sequencer = self.seq;

    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        int32_t delta = enc->delta;

        if (delta != 0 && abs(delta) > 1) {
            switch (i) {
            case 0: // Change Channel
                self.midi_channel =
                    (self.midi_channel + (delta > 0 ? 1 : 15)) & 0x0F;
                self.channel_dirty = true;
                self.sequencer_dirty = true;
                printf("MIDI Channel: %d\n", self.midi_channel + 1);
                break;

            case 1: // Reserved for future use
                break;

            case 2: // Reserved for future use
                break;

            case 3: // Reserved for future use
                break;
            }
        }

        // Handle encoder button presses
        if (!enc->button_state && enc->button_edge) {
            switch (i) {
            case 0: // Play/Pause
                self.sequencer_playing = !self.sequencer_playing;
                if (self.sequencer_playing) {
                    sequencer.pause();
                    printf("Sequencer paused\n");
                } else {
                    sequencer.play();
                    printf("Sequencer playing\n");
                }
                self.sequencer_dirty = true;
                break;

            case 1: // Toggle Auto-stepping
                self.auto_stepping_enabled = !self.auto_stepping_enabled;
                self.sequencer_dirty = true;
                printf("Auto-stepping: %s\n",
                       self.auto_stepping_enabled ? "ON" : "OFF");
                break;

            case 2:                                 // Next Sequencer Menu
                self.ui_state = UI_STATE_SEQUENCER; // or whatever the next
                self.sequencer_settings_dirty = true;        // sequencer menu is
                printf("State: SEQUENCER_TEMPO\n");
                break;

            case 3: // Go to next menu (back to main?)
                self.ui_state = UI_STATE_CHOOSE;
                printf("State: MAIN\n");
                self.channel_dirty = true;
                self.filter_dirty = true;
                self.adsr_dirty = true;
                self.main_dirty = true;
                break;
            }
        }
    }
}

void UiHandler::sequencer_note_edit_update_display(UiHandler &self) {
    HardwareManager &hw = self.hw;
    Sequencer &sequencer = self.seq;
    bool changed = false;

    if (self.sequencer_dirty) {
        // Draw current step number
        // Draw notes on current step by channel
        // Draw auto-stepping status
        // Draw play/pause status
        hw.draw_sequencer_note_edit(
            self.current_sequencer_step, self.midi_channel,
            self.auto_stepping_enabled, sequencer.is_playing(),
            sequencer.get_step_notes(
                self.current_sequencer_step), // You'll need to implement this
            sequencer.get_step_channels(
                self.current_sequencer_step) // You'll need to implement this
        );
        changed = true;
        self.sequencer_dirty = false;
    }

    if (self.channel_dirty) {
        // Update channel display if needed
        changed = true;
        self.channel_dirty = false;
    }

    if (changed) {
        hw.display_show();
    }
}

void UiHandler::sequencer_note_edit_enter(UiHandler &self) {
    // Initialize sequencer note edit mode
    self.current_sequencer_step = 0;
    self.auto_stepping_enabled = false;
    self.sequencer_dirty = true;
    printf("Entered sequencer note edit mode\n");
}
