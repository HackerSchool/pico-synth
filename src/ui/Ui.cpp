#include "Ui.hpp"
#include "HardwareManager.hpp"
#include "MidiHandler.hpp"
#include "Sequencer.hpp"

const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                             66, 68, 70, -1, 67, 69, 71, 72};

UiHandler::UiHandler(HardwareManager &hw, MidiHandler &midi_handler,
                     Sequencer &seq)
    : hw(hw), midi(midi_handler), seq(seq) {}

void UiHandler::update() {
    UiDispatchEntry ui_dispatch_entry = ui_dispatch_table[ui_state];
    ui_dispatch_entry.handle_encoders(*this);
    ui_dispatch_entry.handle_switches(*this);
    ui_dispatch_entry.handle_display(*this);
}

// Helper functions
void UiHandler::set_adsr_param(int param, uint8_t value) {
    switch (param) {
    case 0:
        channel_params[midi_channel].attack = value;
        break;
    case 1:
        channel_params[midi_channel].decay = value;
        break;
    case 2:
        channel_params[midi_channel].sustain = value << 8;
        break;
    case 3:
        channel_params[midi_channel].release = value;
        break;
    }
}

uint8_t UiHandler::get_adsr_param(int param) {
    switch (param) {
    case 0:
        return channel_params[midi_channel].attack;
    case 1:
        return channel_params[midi_channel].decay;
    case 2:
        return channel_params[midi_channel].sustain >> 8;
    case 3:
        return channel_params[midi_channel].release;
    default:
        return 0;
    }
}
