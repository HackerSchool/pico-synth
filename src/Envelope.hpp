#ifndef ENVELOPE_HPP
#define ENVELOPE_HPP

#include <array>
#include <cstdint>
#include <pico/types.h>

// ADSR Envelope Class
class ADSREnvelope {
  public:
    ADSREnvelope(); //Default Constructor
    ADSREnvelope(float a, float d, float s, float r,
                 std::array<int16_t, 1156> &in_signal, float trigger);

    void out();
    void set_trigger(float trig);
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
    float a, d, s, r; // Attack, Decay, Sustain, Release times in seconds
    float trigger;

    EnvelopeState state = ENV_IDLE;
    float current_scale = 0.0f;
    float release_start_level = 0.0f;
    float t = 0;                          // Time tracking for envelope phases
    float sample_delta = 1.0f / 44100.0f; // Buffer time increment
};

#endif // !ENVELOPE_HPP
