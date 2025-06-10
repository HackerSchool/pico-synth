#include "Ui.hpp"
#include "fixed_point.h"
#include <cstdint>


void UiHandler::main_handle_encoders(UiHandler& self, Synth &synth, HardwareManager &hw) {

    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        int32_t delta = enc->delta;

        if (delta != 0 && abs(delta) > 1) {
            switch (i) {
            case 0:
                synth.cycle_wave_type(delta > 0 ? 1 : -1);
                break;

            case 1: {
                int16_t increment = float_to_q1_15(.1f);
                for (auto &env : synth.envelopes) {
                    env.increment_ADSR(self.current_adsr_param,
                                       delta > 0 ? increment : -increment);
                }
                self.adsr_dirty = true;
                break;
            }
            case 2:
                // Only adjust filter cutoff if not in FILTER_OFF mode
                if (synth.current_filter_type != FILTER_OFF) {
                    float cut_off = synth.get_filter_cutoff();
                    float new_cut_off = cut_off + (delta > 0 ? 50.f : -50.f);
                    // Ensure cutoff stays within reasonable bounds
                    new_cut_off = new_cut_off < 20.0f ? 20.0f : new_cut_off;
                    new_cut_off =
                        new_cut_off > 20000.0f ? 20000.0f : new_cut_off;
                    synth.set_filter_cutoff(new_cut_off, 0.5f);
                    // printf("new cut off %f\n", new_cut_off);
                }
                self.filter_dirty = true;
                break;
            }
        }

        if (!enc->button_state && enc->button_edge) {
            if (i == 1) {

                self.current_adsr_param = (self.current_adsr_param + 1) % 4;
                self.adsr_dirty = true;
            }
            if (i == 2) {

                synth.cycle_filter_type();
                self.filter_dirty = true;
            }

            if (i == 3) {
                self.ui_state = UI_STATE_MIDI_SETTINGS;
                printf("State: MIDI_STATE");
            }
        }
    }
}



void UiHandler::main_update_display(UiHandler &self) {
    Synth &synth = self.synth;
    HardwareManager &hw = self.hw;
    bool changed = false;

    std::bitset<128> note_state = synth.get_notes_bitmask();
    if (note_state != self.last_note_state) {
        self.last_note_state = note_state;
        hw.draw_notes();
        changed = true;
    }

    WaveType current = synth.oscillators[0].get_wave_type();
    if (current != self.last_wave_type) {
        self.last_wave_type = current;
        hw.draw_wave_type(current);
        changed = true;
    }

    if (self.adsr_dirty) {
        hw.draw_adsr(self.current_adsr_param); // new function below
        changed = true;
        self.adsr_dirty = false;
    }

    if (self.filter_dirty) {
        hw.draw_filter();
        changed = true;
        self.filter_dirty = false;
    }

    if (changed) {
        // ssd1306_show(&disp);
        hw.display_show();
    }
}
