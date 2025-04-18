#ifndef ENVELOPE_HPP
#define ENVELOPE_HPP

#include <array>
#include <cstdint>
#include <pico/types.h>
#include "fixed_point.h"

// ADSR Envelope Class
class ADSREnvelope {
  public:
    ADSREnvelope(); // Default Constructor
    ADSREnvelope(float a, float d, float s, float r,
                 std::array<int16_t, 1156> &in_signal, float trigger);

    void out();
    void set_trigger(float trig);
    void set_idle();
    std::array<int16_t, 1156> &get_output();

  private:
    enum EnvelopeState {
        ENV_ATTACK,
        ENV_DECAY,
        ENV_SUSTAIN,
        ENV_RELEASE,
        ENV_IDLE
    };

    std::array<int16_t, 1156> *in_signal;
    std::array<int16_t, 1156> output;

    // Use fixed-point arithmetic for envelope (8.24 format)
    q8_24_t a, d, s, r; // Attack, Decay, Sustain, Release times in seconds
    q8_24_t current_scale = 0;
    q8_24_t release_start_level = 0.0f;
    q8_24_t t = 0; // Time tracking for envelope phases

    float trigger;
    EnvelopeState state = ENV_IDLE;
    float sample_delta = 1.0f / 44100.0f; // Buffer time increment

    // Precompute constants
    static constexpr q8_24_t FIXED_ONE = Q24_ONE; // 1.0 in 8.24 format
    static constexpr uint32_t SAMPLE_DELTA = static_cast<uint32_t>(
        1.0f / 44100.0f * FIXED_ONE); // Buffer time increment
};

#endif // !ENVELOPE_HPP
