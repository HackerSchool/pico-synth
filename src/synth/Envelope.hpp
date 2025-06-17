#ifndef ENVELOPE_HPP
#define ENVELOPE_HPP

#include "Wavetable.hpp"
#include "config.hpp"
#include "fixed_point.h"
#include <array>
#include <cstdint>
#include <pico/types.h>

// ADSR Envelope Class
class ADSREnvelope {
  public:
    ADSREnvelope(); // Default Constructor
    ADSREnvelope(uint8_t a, uint8_t d, uint8_t s, uint8_t r);

    void out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer);
    void set_idle();
    void gate_on();
    void gate_off();

    void set_ADSR(uint8_t a_in, uint8_t d_in, uint8_t s_in, uint8_t r_in);

    std::array<int16_t, SAMPLES_PER_BUFFER> &get_output();

    void increment_ADSR(uint8_t which, int16_t delta_q15);

    std::array<uint32_t, 4> get_ADSR();

    void get_ADSR_strings(char out[4][8]);
    void update_deltas();

    uint32_t a, d, s, r; // Attack, Decay, Release times
    uint32_t current_level;

    enum EnvelopeState {
        ENV_ATTACK,
        ENV_DECAY,
        ENV_SUSTAIN,
        ENV_RELEASE,
        ENV_IDLE
    };

    EnvelopeState state = ENV_IDLE;

  private:
    // std::array<int16_t, SAMPLES_PER_BUFFER> *in_signal;
    // std::array<int16_t, SAMPLES_PER_BUFFER> output;

    // Current envelope state
};

#endif // !ENVELOPE_HPP
