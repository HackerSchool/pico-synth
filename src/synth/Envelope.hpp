#ifndef ENVELOPE_HPP
#define ENVELOPE_HPP

#include "config.hpp"
#include "fixed_point.h"
#include <array>
#include <cstdint>
#include <pico/types.h>

// ADSR Envelope Class
class ADSREnvelope {
  public:
    ADSREnvelope(); // Default Constructor
    ADSREnvelope(uint8_t a, uint8_t d, uint8_t s, uint8_t r,
                 std::array<int16_t, SAMPLES_PER_BUFFER> &in_signal);

    void out();
    void set_idle();
    void gate_on();
    void gate_off();

    void set_ADSR(uint8_t a_in, uint8_t d_in, uint8_t s_in, uint8_t r_in);

    std::array<int16_t, SAMPLES_PER_BUFFER> &get_output();

    void increment_ADSR(uint8_t which, int16_t delta_q15);

    std::array<uint16_t, 4> get_ADSR();

    void get_ADSR_strings(char out[4][8]);
    void update_deltas();

  private:
    enum EnvelopeState {
        ENV_ATTACK,
        ENV_DECAY,
        ENV_SUSTAIN,
        ENV_RELEASE,
        ENV_IDLE
    };

    std::array<int16_t, SAMPLES_PER_BUFFER> *in_signal;
    std::array<int16_t, SAMPLES_PER_BUFFER> output;

    //uint8_t parameters to comply with 7 bit MIDI
    uint8_t a, d, r; // Attack, Decay, Release times
    uint16_t s;

    // Current envelope state
    uint16_t current_level; // Q1.15 format (0-0.999)
    uint16_t release_start_level;

    // Pre-calculated deltas (updated when ADSR changes)
    int16_t attack_delta;  // Q2.14 format
    int16_t decay_delta;   // Q2.14 format
    int16_t release_delta; // Q2.14 format

    float trigger;
    EnvelopeState state = ENV_IDLE;

    // static constexpr uint32_t SAMPLE_RATE = 44100;
};

#endif // !ENVELOPE_HPP
