#ifndef OPERATOR_HPP
#define OPERATOR_HPP

#include "Oscillator.hpp"
#include "Envelope.hpp"
#include "Wavetable.hpp"
#include <array>
#include <cstdint>

#define OP_PER_VOICE 2

class Operator {
public:
    Operator();

    void out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer,
                    uint8_t fm_depth_);

    Oscillator osc;
    ADSREnvelope env;
    uint16_t ratio;
    uint16_t feedback;
    uint16_t fm_depth;
};


class Voice {
public:
    Voice();

    void out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer,
                    uint8_t fm_depth_);
    void gate_on();
    void gate_off();

    std::array<Operator, OP_PER_VOICE> op;
    uint8_t midi_note;
    uint8_t midi_channel;


    bool playing = false;
    bool steal = false;
    bool state = false;
    
};

#endif
