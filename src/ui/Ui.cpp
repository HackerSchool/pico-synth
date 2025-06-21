#include "Ui.hpp"
#include "HardwareManager.hpp"
#include "MidiHandler.hpp"
#include "Sequencer.hpp"

const int key_to_midi[16] = {-1, 61, 63, -1, 60, 62, 64, 65,
                             66, 68, 70, -1, 67, 69, 71, 72};

UiHandler::UiHandler(HardwareManager &hw, MidiHandler &midi_handler,
                     Sequencer &seq, Sampler &sampler, Synth &synth)
    : hw(hw), midi(midi_handler), seq(seq), sampler(sampler), synth(synth) {

    // List all WAV files and store them
    wav_files = sampler.list_wav_files(true); // recursive
    wav_files.print_files();                  // Optional: print on startup

    // Optional: Load first file by default
    // if (wav_files.get_count() > 0) {
    //    sampler.load_sample_by_index(0, 0); // Load first file into player 0
    //}

    ui_dispatch_table[UI_STATE_MAIN] = {
        main_handle_encoders, main_handle_switches, main_update_display};
    ui_dispatch_table[UI_STATE_FM_EDIT] = {fm_edit_handle_encoders,
                                           main_handle_switches,
                                           fm_edit_update_display};
    ui_dispatch_table[UI_STATE_MIDI_SETTINGS] = {
        midi_handle_encoders, main_handle_switches, midi_update_display};
    ui_dispatch_table[UI_STATE_SEQUENCER] = {sequencer_handle_encoders,
                                             main_handle_switches,
                                             sequencer_update_display};
    ui_dispatch_table[UI_STATE_SEQUENCER_EDIT] = {
        sequencer_note_edit_handle_encoders,
        sequencer_note_edit_handle_switches,
        sequencer_note_edit_update_display};
    ui_dispatch_table[UI_STATE_CHOOSE] = {
        choose_handle_encoders, main_handle_switches, choose_update_display};
    ui_dispatch_table[UI_STATE_SAMPLER] = {
        sampler_handle_encoders, main_handle_switches, sampler_update_display};
}

void UiHandler::update() {
    UiDispatchEntry ui_dispatch_entry = ui_dispatch_table[ui_state];
    ui_dispatch_entry.handle_encoders(*this);
    tud_task(); // Service USB
    ui_dispatch_entry.handle_switches(*this);
    tud_task(); // Service USB
    ui_dispatch_entry.handle_display(*this);
    tud_task(); // Service USB
}

// // Helper functions
// void UiHandler::set_adsr_param(int param, uint8_t value) {
//     switch (param) {
//     case 0:
//         channel_params[midi_channel].attack = value;
//         break;
//     case 1:
//         channel_params[midi_channel].decay = value;
//         break;
//     case 2:
//         channel_params[midi_channel].sustain = value << 8;
//         break;
//     case 3:
//         channel_params[midi_channel].release = value;
//         break;
//     }
// }
//
uint8_t UiHandler::get_adsr_param(int param) {
    //     switch (param) {
    //     case 0:
    //         return channel_params[midi_channel].attack;
    //     case 1:
    //         return channel_params[midi_channel].decay;
    //     case 2:
    //         return channel_params[midi_channel].sustain >> 8;
    //     case 3:
    //         return channel_params[midi_channel].release;
    //     default:
    //         return 0;
    //     }
    return 0;
}
