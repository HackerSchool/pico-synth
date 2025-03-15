#include "Wavetable.hpp"
#include "Oscillator.hpp"

class Oscillator {
    Oscillator(WaveType wave_type, float freq) {

        // choose the type of wavetable
        switch (wave_type) {
            case Sine:
                wavetable_ = &sine_wave_table;
            case Square:
                wavetable_ = &square_wave_table;
            case Triangle:
                wavetable_ = &triangle_wave_table;
            case Sawtooth:
                wavetable_ = &sawtooth_wave_table;
            default:
                wavetable_ = &sine_wave_table;
        }

        // set the frequncy
        freq = freq;
    }

private:
    const std::array<int16_t, WAVE_TABLE_LEN>* wavetable_;
    float freq;

public:

// TODO: determnine how we want to process the signals, should output an audio buffer but don't quite know how.
};


