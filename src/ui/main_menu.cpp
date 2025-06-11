#include "HardwareManager.hpp"
#include "MidiHandler.hpp"
#include "Ui.hpp"
#include "fixed_point.h"
#include <cstdint>

void UiHandler::main_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;
    MidiHandler &midi = self.midi;

    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        int32_t delta = enc->delta;

        if (delta != 0 && abs(delta) > 1) {
            switch (i) {
            case 0:
                // synth.cycle_wave_type(delta > 0 ? 1 : -1);
                // TODO: cycle wave type
                break;

            case 1: {
                // Adjust currently selected ADSR param
                int param = self.current_adsr_param; // 0=A, 1=D, 2=S, 3=R
                int16_t value = self.adsr[param] + (delta > 0 ? 1 : -1);
                if (value < 0)
                    value = 0;
                if (value > 127)
                    value = 127;
                self.adsr[param] = value;

                const uint8_t cc_map[4] = {73, 75, 70,
                                           72}; // MIDI CCs for A, D, S, R
                uint8_t packet[4] = {0x0B, 0xB0, cc_map[param],
                                     static_cast<uint8_t>(value)};
                midi.midi_receive_note(packet);
                self.adsr_dirty = true;
                break;
            }
            case 2:
                // Only adjust filter cutoff if not in FILTER_OFF mode
                // if (synth.current_filter_type != FILTER_OFF) {
                //     float cut_off = synth.get_filter_cutoff();
                //     float new_cut_off = cut_off + (delta > 0 ? 50.f : -50.f);
                //     // Ensure cutoff stays within reasonable bounds
                //     new_cut_off = new_cut_off < 20.0f ? 20.0f : new_cut_off;
                //     new_cut_off =
                //         new_cut_off > 20000.0f ? 20000.0f : new_cut_off;
                //     synth.set_filter_cutoff(new_cut_off, 0.5f);
                //     // printf("new cut off %f\n", new_cut_off);
                // }
                // TODO: filter cutoff midi
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

                // synth.cycle_filter_type();
                // TODO: filter type midi
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
    // Synth &synth = self.synth;
    HardwareManager &hw = self.hw;
    bool changed = false;

    // std::bitset<128> note_state = synth.get_notes_bitmask();
    // if (note_state != self.last_note_state) {
    //     self.last_note_state = note_state;
    //     hw.draw_notes();
    //     changed = true;
    // }
    //
    // WaveType current = synth.oscillators[0].get_wave_type();
    // if (current != self.last_wave_type) {
    //     self.last_wave_type = current;
    //     hw.draw_wave_type(current);
    //     changed = true;
    // }
    // TODO: get synth state on the UI

    if (self.adsr_dirty) {
        hw.draw_adsr(self.current_adsr_param, self.adsr[0], self.adsr[1],
                     self.adsr[2], self.adsr[3]);
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
