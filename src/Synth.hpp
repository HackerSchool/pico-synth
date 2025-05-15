#ifndef SYNTH_HPP
#define SYNTH_HPP

#include "Envelope.hpp"
#include "Filter.hpp"
#include "MidiHandler.hpp"
#include "Oscillator.hpp"
#include "Wavetable.hpp"
#include "config.hpp"
#include "tusb.h"
#include <bitset>
#include <cstdint>

#define NUM_OSC 8

class Synth {
  public:
    Synth();
    void out();
    std::array<int16_t, SAMPLES_PER_BUFFER> &get_output();
    void process_midi_packet(uint8_t packet[4]);

    void note_on(uint8_t note, uint8_t velocity);
    void note_off(uint8_t note, uint8_t velocity);
    const char *get_notes_playing_names();
    std::bitset<128> get_notes_bitmask() const { return notes_playing_bitset; }

    FilterFIR low_pass = FilterFIR(1000.f);
    FilterCheb low_pass_cheb = FilterCheb(5000.f, 1.f, 44100.f);

    std::array<Oscillator, NUM_OSC> oscillators;
    std::array<ADSREnvelope, NUM_OSC> envelopes;

  private:
    std::array<int16_t, SAMPLES_PER_BUFFER> output = {};

    std::bitset<128> notes_playing_bitset;

    uint8_t osc_midi_note[NUM_OSC] = {};
    bool osc_playing[NUM_OSC] = {};
};

#endif // !SYNTH_HPP
