#ifndef SYNTH_HPP
#define SYNTH_HPP

#include "Envelope.hpp"
#include "Filter.hpp"
#include "MidiHandler.hpp"
#include "Operator.hpp"
#include "Oscillator.hpp"
#include "Wavetable.hpp"

#include "config.hpp"
#include "tusb.h"
#include <bitset>
#include <cstdint>

#define NUM_VOICES 32

extern const WaveType channel_wave_map[16];

// Channel-specific parameters (16 MIDI channels)
struct ChannelParams {
    uint8_t attack = 5;
    uint8_t decay = 5;
    uint16_t sustain = 64 << 8;
    uint8_t release = 5;
    uint8_t filter_cutoff_msb = 64; // Default to mid-range
    uint8_t filter_cutoff_lsb = 0;
    uint8_t filter_q_msb = 16; // Default to lower Q
    uint8_t filter_q_lsb = 0;
};

class Synth {
  public:
    Synth();
    void out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer);

    // TODO: make it into an array of buffers, max 6 should be enough for all
    // algos
    std::array<int16_t, SAMPLES_PER_BUFFER> flow_buffer{0};

    void out_interp();
    std::array<int16_t, SAMPLES_PER_BUFFER> &get_output();
    void process_midi_packet(uint8_t packet[4]);

    void cycle_wave_type(int delta);

    void note_on(uint8_t channel, uint8_t note, uint8_t velocity);
    void note_off(uint8_t channel, uint8_t note, uint8_t velocity);
    const char *get_notes_playing_names();
    std::bitset<128> get_notes_bitmask() const { return notes_playing_bitset; }

    FilterFIR low_pass = FilterFIR(1000.f);
    FilterCheb low_pass_cheb = FilterCheb(5000.f, 0.5f, 44100.f);

    std::array<ChannelParams, 16> channel_params;

    // voice arrays
    // maybe make Ill make this into a struct, but thats the age old question
    std::array<Voice, NUM_VOICES> voice;

    // std::array<Oscillator, NUM_VOICES> oscillators;
    // std::array<ADSREnvelope, NUM_VOICES> envelopes;
    // bool osc_playing[NUM_VOICES] = {};
    // uint8_t osc_midi_note[NUM_VOICES] = {};
    // uint8_t midi_channel[NUM_VOICES] = {};
    // bool osc_steal[NUM_VOICES] = {};


    void cycle_filter_type();

    void set_filter_cutoff(float cutoff, float q = 0.5f);
    void update_filter_cutoff(uint8_t channel);
    void update_filter_q(uint8_t channel);
    void set_filter_type(uint8_t type_value);

    float get_filter_cutoff();
    FilterType current_filter_type = FILTER_OFF; // Default to Chebyshev

  private:
    std::array<int16_t, SAMPLES_PER_BUFFER> output = {};

    std::bitset<128> notes_playing_bitset;
};

#endif // !SYNTH_HPP
