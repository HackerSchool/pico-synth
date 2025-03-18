#include "Synth.hpp"
#include "Oscillator.hpp"
#include "Wavetable.hpp"

Synth::Synth() {
    // init the oscillators and envelopes
    for (int i = 0; i < 8; i++) {
        oscillators[i] = Oscillator(Square, 440.f);
        envelopes[i] =
            ADSREnvelope(0.5f, 0.1f, 0.4f, 1.f, oscillators[i].get_output(), 5.f);
    }
}

void Synth::out() {
    output = {}
    for (int i = 0; i < 8; i++) {
        oscillators[i].out();
        envelopes[i].out();
    }
    for (int i = 0; i < 8; i++) {
        output[i] += envelopes[i] / 8;
    }
}

std::array<int16_t, 1156>& Synth::get_output() {
    return output;
}
