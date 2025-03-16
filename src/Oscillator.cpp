#include "Oscillator.hpp"
#include "Wavetable.hpp"
#include <cstdint>

class Oscillator {
  public:
    Oscillator(WaveType wave_type, float freq) {
        // Choose the type of wavetable
        switch (wave_type) {
            case Sine:
                wavetable_ = &sine_wave_table;
                break;
            case Square:
                wavetable_ = &square_wave_table;
                break;
            case Triangle:
                wavetable_ = &triangle_wave_table;
                break;
            case Sawtooth:
                wavetable_ = &sawtooth_wave_table;
                break;
            default:
                wavetable_ = &sine_wave_table;
                break;
        }

        // Set the frequency
        this->freq = freq;
        step = WAVE_TABLE_LEN * (freq / 44100.0f);  
    }

    std::array<int16_t, 1156> out() {
        std::array<int16_t, 1156> output;
        for (int i = 0; i < 1156; i++) {
            output[i] = wavetable_->at(static_cast<int>(pos));
            pos += step;
            if (pos >= WAVE_TABLE_LEN) {
                pos -= WAVE_TABLE_LEN;
            }
        }
        return output;
    }

  private:
    const std::array<int16_t, WAVE_TABLE_LEN> *wavetable_;
    float freq;
    float step; 
    float pos = 0.0f; 
};
