#ifndef MIDI_HANDLER_HPP
#define MIDI_HANDLER_HPP

// #include "Synth.hpp"
#include "pico/util/queue.h"
#include "tusb.h"
#include <cstdint>

class Synth; // Forward declaration

#define MIDI_MIN 0
#define MIDI_MAX 127

extern const float midi_frequencies[MIDI_MAX + 1];
extern const char *const midi_note_names[128];

// Function declaration (global access)
float midi_to_freq(uint8_t midi_note);

class MidiHandler {
  public:
    MidiHandler(queue_t &midi_queue);

    void midi_task();
    void midi_send_note(uint8_t note, uint8_t velocity, bool on);
    void midi_receive_note(uint8_t *packet);

  private:
    queue_t &midi_queue;
};

#endif // MIDI_HANDLER_HPP
