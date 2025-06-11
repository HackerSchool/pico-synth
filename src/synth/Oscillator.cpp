#include "Oscillator.hpp"
#include "Wavetable.hpp"
#include "config.hpp"
#include "hardware/interp.h"
#include "tusb.h"

const int wave_shift = WAVE_SHIFT;
const int wave_len = WAVE_LEN;
const int wave_max = WAVE_MAX;

// constexpr float SAMPLE_RATE = 44100.0f;
constexpr float MAGIC_NUMBER = 1.5625f; // your overclock factor

const float midi_frequencies[128] = {
    8.1758f,    8.6610f,    9.1770f,   9.7227f,   10.3009f,   10.9134f,
    11.5623f,   12.2499f,   12.9783f,  13.7500f,  14.5676f,   15.4339f,
    16.3516f,   17.3239f,   18.3540f,  19.4454f,  20.6017f,   21.8268f,
    23.1247f,   24.4997f,   25.9565f,  27.5000f,  29.1352f,   30.8677f,
    32.7032f,   34.6478f,   36.7081f,  38.8909f,  41.2034f,   43.6535f,
    46.2493f,   48.9994f,   51.9131f,  55.0000f,  58.2705f,   61.7354f,
    65.4064f,   69.2957f,   73.4162f,  77.7817f,  82.4069f,   87.3071f,
    92.4986f,   97.9989f,   103.826f,  110.000f,  116.541f,   123.471f,
    130.813f,   138.591f,   146.832f,  155.563f,  164.814f,   174.614f,
    184.997f,   195.998f,   207.652f,  220.000f,  233.082f,   246.942f,
    261.626f,   277.183f,   293.665f,  311.127f,  329.628f,   349.228f,
    369.994f,   391.995f,   415.305f,  440.000f,  466.164f,   493.883f,
    523.251f,   554.365f,   587.330f,  622.254f,  659.255f,   698.456f,
    739.989f,   783.991f,   830.609f,  880.000f,  932.328f,   987.767f,
    1046.50f,   1108.73f,   1174.66f,  1244.51f,  1318.51f,   1396.91f,
    1479.98f,   1567.98f,   1661.22f,  1760.00f,  1864.66f,   1975.53f,
    2093.00f,   2217.46f,   2349.32f,  2489.02f,  2637.02f,   2793.83f,
    2959.96f,   3135.96f,   3322.44f,  3520.00f,  3729.31f,   3951.07f,
    4186.01f,   4434.92f,   4698.63f,  4978.03f,  5274.04f,   5587.65f,
    5919.91f,   6271.93f,   6644.88f,  7040.00f,  7458.62f,   7902.13f,
    8372.018f,  8869.844f,  9397.273f, 9956.063f, 10548.080f, 11175.300f,
    11839.820f, 12543.850f,
};

// compile time LUT for the dco_steps
__attribute__((section(".data"))) const std::array<uint32_t, 128> note_table{
    []() {
        std::array<uint32_t, 128> table{};
        for (int i = 0; i < 128; i++) {
            // Calculate step as fixed point 16.16
            float step_f = (WAVE_TABLE_LEN * midi_frequencies[i] *
                            MAGIC_NUMBER * 65536.0f) /
                           SAMPLE_RATE;
            table[i] = static_cast<uint32_t>(step_f);
        }
        return table;
    }()};

Oscillator::Oscillator()
    : freq(440.f), wavetable_(&sine_wave_table), step(0), pos(0) {
} // Default constructor

Oscillator::Oscillator(WaveType wave_type, float freq) : freq(freq) {
    set_wavetable(wave_type);
    set_freq(freq);
}

void Oscillator::set_wavetable(WaveType wave_type) {
    // Choose the wavetable based on wave type
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
    case Sinc:
        wavetable_ = &sinc_table;
        break;
    default:
        wavetable_ = &sine_wave_table;
        break;
    }
    wave_type_ = wave_type;
}

WaveType Oscillator::get_wave_type() { return wave_type_; }

void Oscillator::out() {
    const uint32_t pos_mask = (WAVE_TABLE_LEN << 16) - 1;
    for (int i = 0; i < SAMPLES_PER_BUFFER; i++) {
        // Extract integer and fractional parts
        uint16_t pos_int = pos >> 16;
        uint16_t frac = pos & 0xFFFF; // Bottom 16 bits = fractional part

        // Get current and next samples
        int16_t sample0 = (*wavetable_)[pos_int];
        int16_t sample1 = (*wavetable_)[(pos_int + 1) & (WAVE_TABLE_LEN - 1)];

        // Linear interpolation using shifts instead of division
        // result = sample0 + (sample1 - sample0) * frac / 65536
        // But we use shifts: frac is already 0-65535, so just shift right
        int32_t diff = sample1 - sample0;
        int32_t interpolated = sample0 + ((diff * frac) >> 16);

        output[i] = (int16_t)interpolated;

        pos = (pos + step) & pos_mask;
    }
}

void Oscillator::out_interp() {
    // copy voice state to the interpolator
    interp0->base[0] = dco_step;
    interp0->base[2] = (uintptr_t)wavetable_->data(); // Get the base pointer
    interp0->accum[0] = dco_pos;

    // generate the samples
    for (uint i = 0; i < SAMPLES_PER_BUFFER; ++i) {
        output[i] = *(int16_t *)interp0->pop[2];
    }

    // update voice state
    dco_pos = interp0->accum[0] & (wave_max - 1);
}

void Oscillator::set_dco_step(uint8_t note) {
    dco_step = note_table[note];
    dco_pos = 0;
}

std::array<int16_t, SAMPLES_PER_BUFFER> &Oscillator::get_output() {
    return output;
}

void Oscillator::set_freq(float new_freq) {
    // Convert to fixed-point (16.16 format)
    float magic_number = 1.5625; // its because we overclock from 96 to 150MHz
    step = static_cast<uint32_t>(WAVE_TABLE_LEN * new_freq * magic_number *
                                 65536.0f / 44100.0f);
    // float step_print =
    //     WAVE_TABLE_LEN * new_freq * magic_number * 65536.0f / 44100.0f;
    //
    // printf("Step: %f\n", step_print);
    // pos = 0;
}
