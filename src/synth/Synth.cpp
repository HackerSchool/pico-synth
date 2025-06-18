#include "Synth.hpp"
#include "Envelope.hpp"
#include "Oscillator.hpp"
#include "Wavetable.hpp"
#include "config.hpp"
#include "hardware/interp.h"
#include <array>
#include <cstdint>
#include <cstdio>

const int wave_shift = WAVE_SHIFT;
const int wave_len = WAVE_LEN;
const int wave_max = WAVE_MAX;

// TODO: make an exponential lookup table for ADSR for increased perception!

Synth::Synth() {
    // init the oscillators and envelopes
    for (int i = 0; i < NUM_VOICES; i++) {
        voice[i] = Voice();
    }
}

void Synth::out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer) {

    // cleanup oscillators
    for (int i = 0; i < NUM_VOICES; i++) {
        if (voice[i].playing && voice[i].is_idle()) {
            voice[i].playing = false;
            voice[i].steal = false;
        }
    }

    // set up the interpolator
    interp_config cfg0 = interp_default_config();
    interp_config_set_shift(&cfg0, 15);
    interp_config_set_mask(&cfg0, 1, wave_shift);
    interp_config_set_add_raw(&cfg0, true);
    interp_set_config(interp0, 0, &cfg0);

    interp_config cfg1 = interp_default_config();
    interp_config_set_shift(&cfg1, 15);
    interp_config_set_mask(&cfg1, 1, wave_shift);
    interp_config_set_add_raw(&cfg1, true);
    interp_set_config(interp1, 0, &cfg1);

    for (int i = 0; i < NUM_VOICES; i++) {
        if (!voice[i].playing) {
            continue;
        }
        voice[i].out(flow_buffer, channel_params[0].filter_cutoff_msb);
        for (int k = 0; k < SAMPLES_PER_BUFFER; k++) {
            // divide by 8
            buffer[k] += flow_buffer[k] >> 4;
        }
    }

    // low_pass.out(buffer.data(), buffer.size());
    // low_pass_cheb.out(buffer.data(), buffer.size());

    // Apply the selected filter
    // switch (current_filter_type) {
    // case FILTER_LOW_PASS:
    //     low_pass.out(buffer.data(), buffer.size());
    //     break;
    // case FILTER_CHEBYSHEV:
    //     low_pass_cheb.out(buffer.data(), buffer.size());
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
            // case 73: // Attack
            //     channel_params[channel].attack = velocity;
            //     printf("attack changed: %d\n", velocity);
            //     for (int i = 0; i < NUM_VOICES; i++) {
            //         if (midi_channel[i] == channel) {
            //             envelopes[i].set_ADSR(channel_params[channel].attack,
            //                                   channel_params[channel].decay,
            //                                   channel_params[channel].sustain
            //                                   >> 8,
            //                                   channel_params[channel].release);
            //         }
            //     }
            //     break;
            // case 75: // Decay
            //     channel_params[channel].decay = velocity;
            //     printf("decay changed: %d\n", velocity);
            //     for (int i = 0; i < NUM_VOICES; i++) {
            //         if (midi_channel[i] == channel) {
            //             envelopes[i].set_ADSR(channel_params[channel].attack,
            //                                   channel_params[channel].decay,
            //                                   channel_params[channel].sustain
            //                                   >> 8,
            //                                   channel_params[channel].release);
            //         }
            //     }
            //     break;
            // case 70: // Sustain
            //     channel_params[channel].sustain = velocity << 8;
            //     printf("sustain changed: %d\n", velocity);
            //     for (int i = 0; i < NUM_VOICES; i++) {
            //         if (midi_channel[i] == channel) {
            //             envelopes[i].set_ADSR(channel_params[channel].attack,
            //                                   channel_params[channel].decay,
            //                                   channel_params[channel].sustain
            //                                   >> 8,
            //                                   channel_params[channel].release);
            //         }
            //     }
            //     break;
            // case 72: // Release
            //     channel_params[channel].release = velocity;
            //     printf("release changed: %d\n", velocity);
            //     for (int i = 0; i < NUM_VOICES; i++) {
            //         if (midi_channel[i] == channel) {
            //             envelopes[i].set_ADSR(channel_params[channel].attack,
            //                                   channel_params[channel].decay,
            //                                   channel_params[channel].sustain
            //                                   >> 8,
            //                                   channel_params[channel].release);
            //         }
            //     }
            //     break;

        case 16: // Filter Cutoff MSB (CC 16)
            channel_params[channel].filter_cutoff_msb = velocity;
            update_filter_cutoff(channel);
            break;
        case 48: // Filter Cutoff LSB (CC 48)
            channel_params[channel].filter_cutoff_lsb = velocity;
            update_filter_cutoff(channel);
            break;
        case 17: // Filter Resonance/Q MSB (CC 17)
            channel_params[channel].filter_q_msb = velocity;
            update_filter_q(channel);
            break;
        case 49: // Filter Resonance/Q LSB (CC 49)
            channel_params[channel].filter_q_lsb = velocity;
            update_filter_q(channel);
            break;
        case 18: // Filter Type Selection (CC 18)
            set_filter_type(velocity);
            printf("Filter type changed: %d\n", velocity);
            break;
        }
        break;
    } break;
    }
}

void Synth::update_filter_cutoff(uint8_t channel) {
    // // Combine MSB and LSB for 14-bit resolution
    // uint16_t cutoff_14bit = (channel_params[channel].filter_cutoff_msb << 7)
    // |
    //                         channel_params[channel].filter_cutoff_lsb;
    //
    // // Map 14-bit value (0-16383) to frequency range (500-10000 Hz)
    // float cutoff_freq =
    //     1500.0f + (cutoff_14bit * (10000.0f - 500.0f)) / 16383.0f;
    //
    // // Get Q value - FIXED RANGE: 0.3 to 1.0
    // uint16_t q_14bit = (channel_params[channel].filter_q_msb << 7) |
    //                    channel_params[channel].filter_q_lsb;
    // float q_value =
    //     0.3f + (q_14bit * (1.0f - 0.3f)) / 16383.0f; // Q range 0.3-1.0
    //
    // set_filter_cutoff(cutoff_freq, q_value);
    // printf("Filter cutoff: %.2f Hz, Q: %.2f\n", cutoff_freq, q_value);
}

void Synth::update_filter_q(uint8_t channel) {
    // Same logic as cutoff - recalculate both when Q changes
    update_filter_cutoff(channel);
}

void Synth::set_filter_type(uint8_t type_value) {
    if (type_value < 43) {
        current_filter_type = FILTER_OFF;
    } else if (type_value < 85) {
        current_filter_type = FILTER_LOW_PASS; // FIR sync
    } else {
        current_filter_type = FILTER_CHEBYSHEV;
    }
}

const WaveType channel_wave_map[16] = {
    Sine,     Square, Triangle, Sawtooth, Sinc,     Sine,     Square, Triangle,
    Sawtooth, Sinc,   Sine,     Square,   Triangle, Sawtooth, Sinc,   Sine};

void Synth::note_on(uint8_t channel, uint8_t note, uint8_t velocity) {
    // Check if note is playing
    int voice_index = -1;
    for (int i = 0; i < NUM_VOICES; i++) {
        if (voice[i].midi_note == note && voice[i].playing && !voice[i].steal &&
            voice[i].midi_channel == channel) {
            printf("Note already playing: note=%d, velocity=%d\n", note,
                   velocity);
            voice_index = i;
            break;
        }
    }

    // Check if there are any free oscilators
    if (voice_index == -1) {
        for (int i = 0; i < NUM_VOICES; i++) {
            if (!voice[i].playing) {
                voice[i].midi_channel = channel;
                voice[i].playing = true;
                voice[i].midi_note = note;
                notes_playing_bitset.set(note);
                WaveType wt = channel_wave_map[channel];
                voice[i].op[1].osc.set_wavetable(wt);
                voice[i].op[1].osc.set_dco_step(note);
                voice[i].op[0].osc.set_dco_step(note + 12);
                // voice[i].state = 1;

                // // Apply channel ADSR params
                // envelopes[i].set_ADSR(channel_params[channel].attack,
                //                       channel_params[channel].decay,
                //                       channel_params[channel].sustain >>
                //                           8, // Convert back from 16-bit
                //                       channel_params[channel].release);
                // envelopes[i].gate_on();
                // printf("New Note: note=%d, velocity=%d\n", note, velocity);
                voice[i].gate_on();
                break;
            }
        }
    }
}

void Synth::note_off(uint8_t channel, uint8_t note, uint8_t velocity) {
    // Check if note is playing
    // printf("I am in note off");
    for (int i = 0; i < NUM_VOICES; i++) {
        if (voice[i].midi_note == note && voice[i].playing && !voice[i].steal &&
            voice[i].midi_channel == channel) {
            // envelopes[i].set_trigger(0.f);
            voice[i].gate_off();
            // osc_playing[i] = false;
            voice[i].steal = true;
            // voice[i].state = false;
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

// void Synth::cycle_wave_type(int delta) {
//     WaveType wave_type = oscillators[0].get_wave_type();
//     int new_index = static_cast<int>(wave_type) + delta;
//
//     // Wrap around the enum range
//     const int max_wave = static_cast<int>(WaveType::Sinc);
//     if (new_index > max_wave)
//         new_index = 0;
//     if (new_index < 0)
//         new_index = max_wave;
//
//     wave_type = static_cast<WaveType>(new_index);
//
//     for (auto &osc : oscillators) {
//         osc.set_wavetable(wave_type);
//     }
//
//     // printf("Waveform set to: %d\n", wave_type);
// }

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
