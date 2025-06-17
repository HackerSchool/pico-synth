#include "Operator.hpp"
#include "config.hpp"
#include <cstdint>

Operator::Operator() {
    osc = Oscillator(Sine, 440.f);
    env = ADSREnvelope(1, 1, 100, 1, osc.get_output());
}

void Operator::out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer) {
    osc.out(buffer);
    env.out(buffer);
}
void Operator::out_modulated(
    std::array<int16_t, SAMPLES_PER_BUFFER> &buffer,
    std::array<int16_t, SAMPLES_PER_BUFFER> &modulation) {
    osc.out_modulated()
}
