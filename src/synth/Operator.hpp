#ifndef OPERATOR_HPP
#define OPERATOR_HPP

#include "Envelope.hpp"
#include "Oscillator.hpp"
#include "Wavetable.hpp"
#include <array>
#include <cstdint>

#define OP_PER_VOICE 2

struct OperatorParams {
    WaveType wave_type = WaveType::Sine;

    uint8_t attack = 0;
    uint8_t decay = 0;
    uint8_t sustain = 127;
    uint8_t release = 0;

    uint16_t ratio = 1;    // frequency ratio (e.g., 2:1 modulator:carrier)
    uint16_t feedback = 0; // feedback amount
    uint16_t fm_depth =
        0; // how much FM this operator applies (scaled 0–127 or 0–255)
};

struct Patch {
    OperatorParams ops[OP_PER_VOICE]; // One per operator

    uint8_t algorithm = 0; // Future use: op routing
    uint8_t volume = 127;  // Global volume, 0–127
    uint8_t pan = 64;      // Optional stereo pan

    // Add more modulation routings or macros here in the future
};

class Operator {
  public:
    Operator();

    void out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer,
             uint16_t fm_depth_);

    Oscillator osc;
    ADSREnvelope env;
    uint16_t ratio;
    uint16_t feedback;
    uint16_t fm_depth;
};

class Voice {
  public:
    Voice();

    void out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer);
    void gate_on();
    void gate_off();
    bool is_idle();

    void apply_patch(const Patch &patch);

    std::array<Operator, OP_PER_VOICE> op;
    uint8_t midi_note;
    uint8_t midi_channel;

    bool playing = false;
    bool steal = false;
    bool state = false;
};

#endif
