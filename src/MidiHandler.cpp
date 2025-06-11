#include "MidiHandler.hpp"
#include "Envelope.hpp"
#include "Oscillator.hpp"
#include "Synth.hpp"
#include "tusb.h"
#include <cstdint>
#include <cstdio>

// Convert MIDI note to frequency (global function)
float midi_to_freq(uint8_t midi_note) {
    return (midi_note <= MIDI_MAX) ? midi_frequencies[midi_note] : 0.0f;
}

// Constructor
MidiHandler::MidiHandler(queue_t &midi_queue) : midi_queue(midi_queue) {}

// Process incoming MIDI
void MidiHandler::midi_task() {
    uint8_t packet[4];

    while (tud_midi_available()) {
        tud_midi_packet_read(packet);
        queue_add_blocking(&midi_queue, packet);
    }
}

void MidiHandler::midi_receive_note(uint8_t *packet) {
    if (!packet) {
        // Optionally: log an error or assert
        printf("Error: null MIDI packet received\n");
        return;
    }
    queue_add_blocking(&midi_queue, packet);
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
