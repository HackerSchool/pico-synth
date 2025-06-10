#include "MidiHandler.hpp"
#include "Envelope.hpp"
#include "Oscillator.hpp"
#include "Synth.hpp"
#include "tusb.h"
#include <cstdint>
#include <cstdio>

// Convert MIDI note to frequency (global function)
float midi_to_freq(uint8_t midi_note) {
    // printf("note: %f\n", midi_frequencies[midi_note]);
    return (midi_note <= MIDI_MAX) ? midi_frequencies[midi_note] : 0.0f;
}

// Example note sequence
const uint8_t MidiHandler::note_sequence[64] = {
    74, 78, 81, 86,  90, 93, 98, 102, 57, 61,  66, 69, 73, 78, 81, 85,
    88, 92, 97, 100, 97, 92, 88, 85,  81, 78,  74, 69, 66, 62, 57, 62,
    66, 69, 74, 78,  81, 86, 90, 93,  97, 102, 97, 93, 90, 85, 81, 78,
    73, 68, 64, 61,  56, 61, 64, 68,  74, 78,  81, 86, 90, 93, 98, 102};

// Constructor
MidiHandler::MidiHandler(Synth &synth) : synth(synth) {}

// Process incoming MIDI
void MidiHandler::midi_task(queue_t &midi_queue) {
    uint8_t packet[4];

    while (tud_midi_available()) {
        tud_midi_packet_read(packet);
        synth.process_midi_packet(packet);
        queue_add_blocking(&midi_queue, packet);
    }

}

void MidiHandler::midi_send_note(uint8_t note, uint8_t velocity, bool on) {
    uint8_t const cable_num = 0;
    uint8_t const channel = 0;

    if (on) {

        uint8_t note_on[3] = {0x90 | channel, note, velocity};
        tud_midi_stream_write(cable_num, note_on, 3);
    } else {

        uint8_t note_off[3] = {0x80 | channel, note, 0};
        tud_midi_stream_write(cable_num, note_off, 3);
    }
}
