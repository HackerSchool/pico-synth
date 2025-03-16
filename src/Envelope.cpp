#include "Envelope.hpp"
#include <array>
#include <cstdint>

class ADSREnvelope {
  public:
    ADSREnvelope(float a, float d, float s, float r,
                 std::array<int16_t, 1156> &in_signal, float trigger)
        : a(a), d(d), s(s), r(r), in_signal(&in_signal), trigger(trigger) {
        ;
    }

    void out() {

        float scale;
        // note released
        if (trigger < 4.5 && state != ENV_RELEASE && state != ENV_IDLE) {
            release_start_level =
                current_scale; // Store current level for release
            t = 0;             // Reset time for release phase
            state = ENV_RELEASE;
        }

        for (int i = 0; i < 1156; i++) {
            // Calculate the envelope scale based on current state
            switch (state) {
            case ENV_ATTACK:
                scale = t / a;
                if (t >= a) {
                    state = ENV_DECAY;
                    t = 0; // Reset time for decay phase
                }
                break;

            case ENV_DECAY:
                scale = 1.0f - ((1.0f - s) * (t / d));
                if (t >= d) {
                    state = ENV_SUSTAIN;
                    t = 0; // Reset time for sustain phase
                }
                break;

            case ENV_SUSTAIN:
                scale = s; // Sustain is constant
                break;

            case ENV_RELEASE:
                if (trigger > 4.5) {
                    state = ENV_ATTACK;
                    t = 0;
                }
                scale = release_start_level * (1.0f - (t / r));
                if (t >= r) {
                    scale = 0.0f;
                    state = ENV_IDLE;
                }
                break;

            case ENV_IDLE:
                if (trigger > 4.5) {
                    state = ENV_ATTACK;
                    t = 0;
                }
                scale = 0.0f;
                break;
            }

            current_scale = scale; // Store current scale for potential release

            // Apply envelope to the input signal
            output[i] = static_cast<int16_t>((*in_signal)[i] * scale);

            t += sample_delta;
        }
    }
    void set_trigger(float trig) { trigger = trig; }

    std::array<int16_t, 1156> &get_output() { return output; }

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

    // Current state of the envelope
    EnvelopeState state = ENV_IDLE;

    // Current scale and release starting level
    float current_scale = 0.0f;
    float release_start_level = 0.0f;

    // Local time in the envelope
    float t = 0;

    // Buffer time increment
    float sample_delta = 1.0f / 44100.0f;
};
