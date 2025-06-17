#ifndef OPERATOR_HPP
#define OPERATOR_HPP

#include "Oscillator.hpp"
#include "Envelope.hpp"
#include "Wavetable.hpp"
#include <cstdint>

class Operator {
public:
    Operator();

    void out();
    void out_modulated(int16_t *fm_mod, size_t size);

    Oscillator osc;
    ADSREnvelope env;
    uint16_t ratio;
    uint16_t feedback;
}


#endif
