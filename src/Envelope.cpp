#include "Envelope.hpp"

ADSREnvelope::ADSREnvelope(float a, float d, float s, float r, std::array<int16_t, 1156>& in_signal, float trigger)
    : in_signal(&in_signal), a(a), d(d), s(s), r(r), trigger(trigger) {}

void ADSREnvelope::out() {
    float scale = 0;

    // Note released
    if (trigger < 4.5f && state != ENV_RELEASE && state != ENV_IDLE) {
        release_start_level = current_scale;
        t = 0;
        state = ENV_RELEASE;
    }

    for (uint i = 0; i < 1156; i++) {
        switch (state) {
        case ENV_ATTACK:
            scale = t / a;
            if (t >= a) {
                state = ENV_DECAY;
                t = 0;
            }
            break;

        case ENV_DECAY:
            scale = 1.0f - ((1.0f - s) * (t / d));
            if (t >= d) {
                state = ENV_SUSTAIN;
                t = 0;
            }
            break;

        case ENV_SUSTAIN:
            scale = s;
            break;

        case ENV_RELEASE:
            if (trigger > 4.5f) {
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
            if (trigger > 4.5f) {
                state = ENV_ATTACK;
                t = 0;
            }
            scale = 0.0f;
            break;
        }

        current_scale = scale;
        output[i] = static_cast<int16_t>((*in_signal)[i] * scale);
        t += sample_delta;
    }
}

void ADSREnvelope::set_trigger(float trig) {
    trigger = trig;
}

std::array<int16_t, 1156>& ADSREnvelope::get_output() {
    return output;
}
