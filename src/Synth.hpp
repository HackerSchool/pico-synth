#include "Envelope.hpp"
#include "Oscillator.hpp"
#include "Wavetable.hpp"
#include <cstdint>

class Synth {
  public:
    Synth();
    void out();
    std::array<int16_t, 1156> &get_output();

  private:
    std::array<Oscillator, 8> oscillators;
    std::array<ADSREnvelope, 8> envelopes;
    std::array<int16_t, 1156> output = {};
};
