#include "HardwareManager.hpp"
#include "Ui.hpp"

namespace {
int32_t engine_encoder_accumulator = 0;
constexpr int kEngineDetentThreshold = 4;
constexpr int kEngineCount = 2;

inline int abs_int(int value) {
    return value < 0 ? -value : value;
}
}

void UiHandler::engine_select_handle_encoders(UiHandler &self) {
    HardwareManager &hw = self.hw;

    for (int i = 0; i < NUM_ENCODERS; ++i) {
        Encoder *enc = &hw.encoders[i];
        int32_t delta = enc->delta;

        if (UiHandler::encoder_moved(delta) && i == 3) {
            engine_encoder_accumulator += delta;

            if (abs_int(static_cast<int>(engine_encoder_accumulator)) >=
                kEngineDetentThreshold) {
                const int dir =
                    UiHandler::encoder_velocity_delta(engine_encoder_accumulator, 1, 1, 1);
                engine_encoder_accumulator = 0;

                self.engine_select_index =
                    (self.engine_select_index + dir + kEngineCount) %
                    kEngineCount;
                self.engine_select_dirty = true;
            }
        }

        if (!enc->button_state && enc->button_edge) {
            switch (i) {
            case 0:
                self.ui_state = UI_STATE_CHOOSE;
                self.chosen_index = UI_STATE_MAIN;
                self.chosen_dirty = true;
                self.engine_select_dirty = true;
                break;
            case 3:
                self.synth.set_engine(self.engine_select_index == 0
                                          ? SynthEngine::FM
                                          : SynthEngine::KarplusStrong);
                self.ui_state = UI_STATE_MAIN;
                self.main_dirty = true;
                self.channel_dirty = true;
                self.adsr_dirty = true;
                self.filter_dirty = true;
                self.engine_select_dirty = true;
                break;
            default:
                self.engine_select_dirty = true;
                break;
            }
        }
    }
}

void UiHandler::engine_select_update_display(UiHandler &self) {
    if (self.engine_select_dirty) {
        self.hw.draw_engine_select_menu(
            self.engine_select_index,
            static_cast<int>(self.synth.get_engine()));
        self.hw.display_show();
        self.engine_select_dirty = false;
    }
}
