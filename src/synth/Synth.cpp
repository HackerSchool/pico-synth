#include "Synth.hpp"
#include "Envelope.hpp"
#include "Oscillator.hpp"
#include "Wavetable.hpp"
#include "config.hpp"
#include "hardware/interp.h"
#include <cstdint>
#include <cstdio>

const int wave_shift = WAVE_SHIFT;
const int wave_len = WAVE_LEN;
const int wave_max = WAVE_MAX;

// TODO: make an exponential lookup table for ADSR for increased perception!

Synth::Synth() {
    // init the oscillators and envelopes
    for (int i = 0; i < NUM_OSC; i++) {
        oscillators[i] = Oscillator(Sine, 440.f);
        envelopes[i] = ADSREnvelope(1, 1, 100, 1, oscillators[i].get_output());
    }
}

void Synth::out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer) {

    // cleanup oscillators
    for (int i = 0; i < NUM_OSC; i++) {
        if (osc_playing[i] and envelopes[i].state == ADSREnvelope::ENV_IDLE) {
            osc_playing[i] = false;
            osc_steal[i] = false;
        }
    }

    // set up the interpolator
    interp_config cfg = interp_default_config();
    interp_config_set_shift(&cfg, 15);
    interp_config_set_mask(&cfg, 1, wave_shift);
    interp_config_set_add_raw(&cfg, true);
    interp_set_config(interp0, 0, &cfg);

    for (int i = 0; i < NUM_OSC; i++) {
        // oscillators[i].out();
        oscillators[i].out_interp();
        envelopes[i].out();
    }
    for (int i = 0; i < NUM_OSC; i++) {
        std::array<int16_t, SAMPLES_PER_BUFFER> &env_out_i =
            envelopes[i].get_output();
        for (int k = 0; k < SAMPLES_PER_BUFFER; k++) {
            // divide by 8
            buffer[k] += env_out_i[k] >> 4;
        }
    }

    // low_pass.out(buffer.data(), buffer.size());
    // low_pass_cheb.out(buffer.data(), buffer.size());

    // // Apply the selected filter
    // switch (current_filter_type) {
    // case FILTER_LOW_PASS:
    // low_pass.out(buffer.data(), buffer.size());
    //     break;
    // case FILTER_CHEBYSHEV:
    //     low_pass_cheb.out(output.data(), output.size());
    //     break;
    // default:
    //     // No filtering
    //     break;
    // }
}

std::array<int16_t, SAMPLES_PER_BUFFER> &Synth::get_output() { return output; }

void Synth::process_midi_packet(uint8_t packet[4]) {
    uint8_t msg_type = packet[1] & 0xF0;
    uint8_t channel = packet[1] & 0x0F;
    uint8_t note = packet[2];
    uint8_t velocity = packet[3];

    switch (msg_type) {
    case 0x90: // Note On
        if (velocity > 0) {
            // Note on with velocity
            // printf("Note On: channel=%d, note=%d, velocity=%d\n", channel,
            // note,
            //        velocity);
            note_on(channel, note, velocity);
        } else {
            // Note on with velocity 0 is equivalent to Note Off
            // printf("Note Off (via Note On): channel=%d, note=%d\n", channel,
            // note);
            note_off(channel, note, velocity);
        }
        break;

    case 0x80: // Note Off
        // printf("Note Off: channel=%d, note=%d, velocity=%d\n",
        // channel, note,
        // velocity);

        note_off(channel, note, velocity);
        break;

    case 0xB0: { // Control Change

        switch (note) {
        case 73: // Attack
            channel_params[channel].attack = velocity;
            printf("attack changed: %d\n", velocity);
            for (int i = 0; i < NUM_OSC; i++) {
                if (midi_channel[i] == channel) {
                    envelopes[i].set_ADSR(channel_params[channel].attack,
                                          channel_params[channel].decay,
                                          channel_params[channel].sustain >> 8,
                                          channel_params[channel].release);
                }
            }
            break;
        case 75: // Decay
            channel_params[channel].decay = velocity;
            printf("decay changed: %d\n", velocity);
            for (int i = 0; i < NUM_OSC; i++) {
                if (midi_channel[i] == channel) {
                    envelopes[i].set_ADSR(channel_params[channel].attack,
                                          channel_params[channel].decay,
                                          channel_params[channel].sustain >> 8,
                                          channel_params[channel].release);
                }
            }
            break;
        case 70: // Sustain
            channel_params[channel].sustain = velocity << 8;
            printf("sustain changed: %d\n", velocity);
            for (int i = 0; i < NUM_OSC; i++) {
                if (midi_channel[i] == channel) {
                    envelopes[i].set_ADSR(channel_params[channel].attack,
                                          channel_params[channel].decay,
                                          channel_params[channel].sustain >> 8,
                                          channel_params[channel].release);
                }
            }
            break;
        case 72: // Release
            channel_params[channel].release = velocity;
            printf("release changed: %d\n", velocity);
            for (int i = 0; i < NUM_OSC; i++) {
                if (midi_channel[i] == channel) {
                    envelopes[i].set_ADSR(channel_params[channel].attack,
                                          channel_params[channel].decay,
                                          channel_params[channel].sustain >> 8,
                                          channel_params[channel].release);
                }
            }
            break;
        }
        break;
    }
    }
}

const WaveType channel_wave_map[16] = {
    Sine,     Square, Triangle, Sawtooth, Sinc,     Sine,     Square, Triangle,
    Sawtooth, Sinc,   Sine,     Square,   Triangle, Sawtooth, Sinc,   Sine};

void Synth::note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
    // Check if note is playing
    int osc_index = -1;
    for (int i = 0; i < NUM_OSC; i++) {
        if (osc_midi_note[i] == note && osc_playing[i] && !osc_steal[i] &&
            midi_channel[i] == channel) {
            printf("Note already playing: note=%d, velocity=%d\n", note,
                   velocity);
            osc_index = i;
            break;
        }
    }

    // Check if there are any free oscilators
    if (osc_index == -1) {
        for (int i = 0; i < NUM_OSC; i++) {
            if (!osc_playing[i]) {
                midi_channel[i] = channel;
                osc_playing[i] = true;
                osc_midi_note[i] = note;
                notes_playing_bitset.set(note);
                WaveType wt = channel_wave_map[channel];
                oscillators[i].set_wavetable(wt);
                oscillators[i].set_dco_step(note);

                // Apply channel ADSR params
                envelopes[i].set_ADSR(channel_params[channel].attack,
                                      channel_params[channel].decay,
                                      channel_params[channel].sustain >>
                                          8, // Convert back from 16-bit
                                      channel_params[channel].release);
                envelopes[i].gate_on();
                // printf("New Note: note=%d, velocity=%d\n", note, velocity);
                break;
            }
        }
    }
}

void Synth::note_off(uint8_t channel, uint8_t note, uint8_t velocity) {
    // Check if note is playing
    // printf("I am in note off");
    for (int i = 0; i < NUM_OSC; i++) {
        if (osc_midi_note[i] == note && osc_playing[i] && !osc_steal[i] &&
            midi_channel[i] == channel) {
            // envelopes[i].set_trigger(0.f);
            envelopes[i].gate_off();
            // osc_playing[i] = false;
            osc_steal[i] = true;
            notes_playing_bitset.reset(note);
            // printf("Note Off on Synth: note=%d, velocity=%d\n", note,
            // velocity);
            break;
        }
    }
}

const char *Synth::get_notes_playing_names() {
    static char buffer[64]; // Adjust size as needed
    int pos = 0;

    for (int note = 0; note < 128; ++note) {
        if (notes_playing_bitset.test(note)) {
            const char *name = midi_note_names[note];
            int written =
                snprintf(buffer + pos, sizeof(buffer) - pos, "%s,", name);
            if (written < 0 || written >= (int)(sizeof(buffer) - pos)) {
                // Truncated or error
                break;
            }
            pos += written;
        }
    }

    // Remove trailing comma if present
    if (pos > 0 && buffer[pos - 1] == ',') {
        buffer[pos - 1] = '\0';
    } else {
        buffer[pos] = '\0';
    }

    return buffer;
}

void Synth::cycle_wave_type(int delta) {
    WaveType wave_type = oscillators[0].get_wave_type();
    int new_index = static_cast<int>(wave_type) + delta;

    // Wrap around the enum range
    const int max_wave = static_cast<int>(WaveType::Sinc);
    if (new_index > max_wave)
        new_index = 0;
    if (new_index < 0)
        new_index = max_wave;

    wave_type = static_cast<WaveType>(new_index);

    for (auto &osc : oscillators) {
        osc.set_wavetable(wave_type);
    }

    // printf("Waveform set to: %d\n", wave_type);
}

void Synth::cycle_filter_type() {
    current_filter_type =
        static_cast<FilterType>((current_filter_type + 1) % NUM_FILTER_TYPES);
}

void Synth::set_filter_cutoff(float cutoff, float q) {
    switch (current_filter_type) {
    case FILTER_LOW_PASS:
        low_pass.set_cutoff_freq(cutoff);
        break;
    case FILTER_CHEBYSHEV:
        low_pass_cheb.set_cutoff_freq(cutoff, q);
        break;
    default:
        break;
    }
}

float Synth::get_filter_cutoff() {
    switch (current_filter_type) {
    case FILTER_LOW_PASS:
        return low_pass.get_cutoff();
    case FILTER_CHEBYSHEV:
        return low_pass_cheb.get_cutoff();
    default:
        return 0.0f;
    }
}
