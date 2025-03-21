#ifndef SYNTH_HPP
#define SYNTH_HPP

#include "Envelope.hpp"
#include "Oscillator.hpp"
#include "Wavetable.hpp"
#include "MidiHandler.hpp"
#include <cstdint>
#include "tusb.h"
#include "Filter.hpp"

#define NUM_OSC 4

class Synth {
  public:
    Synth();
    void out();
    std::array<int16_t, 1156> &get_output();
    void process_midi_packet(uint8_t packet[4]);

    void note_on(uint8_t note, uint8_t velocity);
    void note_off(uint8_t note, uint8_t velocity);

    FilterFIR low_pass = FilterFIR(1000.f);
    FilterCheb low_pass_cheb = FilterCheb(5000.f, 0.5f, 44100.f);
  private:
    std::array<Oscillator, NUM_OSC> oscillators;
    std::array<ADSREnvelope, NUM_OSC> envelopes;
    std::array<int16_t, 1156> output = {};

    uint8_t osc_midi_note[NUM_OSC] = {};
    bool osc_playing[NUM_OSC] = {};
};

#endif // !SYNTH_HPP
