#include "Envelope.hpp"
#include "Oscillator.hpp"
#include "Synth.hpp"
#include "tusb.h"
#include "MidiHandler.hpp"

// Convert MIDI note to frequency (global function)
float midi_to_freq(uint8_t midi_note) {
    return (midi_note <= MIDI_MAX) ? midi_frequencies[midi_note] : 0.0f;
}

// Example note sequence
const uint8_t MidiHandler::note_sequence[64] = {
    74, 78, 81, 86,  90, 93, 98, 102, 57, 61,  66, 69, 73, 78, 81, 85,
    88, 92, 97, 100, 97, 92, 88, 85,  81, 78,  74, 69, 66, 62, 57, 62,
    66, 69, 74, 78,  81, 86, 90, 93,  97, 102, 97, 93, 90, 85, 81, 78,
    73, 68, 64, 61,  56, 61, 64, 68,  74, 78,  81, 86, 90, 93, 98, 102
};

// Constructor
MidiHandler::MidiHandler(Synth& synth) : synth(synth) {}

// Process incoming MIDI
void MidiHandler::midi_task() {
    uint8_t packet[4];

    while (tud_midi_available()) {
        tud_midi_packet_read(packet);
        synth.process_midi_packet(packet);
    }

    // static uint32_t start_ms = 0;
    // uint32_t current_ms = to_ms_since_boot(get_absolute_time());
    //
    // if (current_ms - start_ms < 286) return;
    // start_ms = current_ms;
    //
    // size_t sequence_size = std::size(note_sequence);
    // size_t previous = (note_pos == 0) ? (sequence_size - 1) : (note_pos - 1);
    //
    // uint8_t const cable_num = 0;
    // uint8_t const channel = 0;
    //
    // uint8_t note_on[3] = {0x90 | channel, note_sequence[note_pos], 127};
    // tud_midi_stream_write(cable_num, note_on, 3);
    //
    // uint8_t note_off[3] = {0x80 | channel, note_sequence[previous], 0};
    // tud_midi_stream_write(cable_num, note_off, 3);
    //
    // note_pos = (note_pos + 1) % sequence_size;
}
