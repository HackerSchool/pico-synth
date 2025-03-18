#ifndef SYNTH_HPP
#define SYNTH_HPP

#include "Envelope.hpp"
#include "Oscillator.hpp"
#include "Wavetable.hpp"
#include "MidiHandler.hpp"
#include <cstdint>
#include "tusb.h"

#define NUM_OSC 8

class Synth {
  public:
    Synth();
    void out();
    std::array<int16_t, 1156> &get_output();
    void process_midi_packet(uint8_t packet[4]);

    void note_on(uint8_t note, uint8_t velocity);
    void note_off(uint8_t note, uint8_t velocity);

  private:
    std::array<Oscillator, NUM_OSC> oscillators;
    std::array<ADSREnvelope, NUM_OSC> envelopes;
    std::array<int16_t, 1156> output = {};

    uint8_t osc_midi_note[NUM_OSC] = {};
    bool osc_playing[NUM_OSC] = {};
};

#endif // !SYNTH_HPP
