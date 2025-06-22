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
    // Initialize patches for all 16 channels
    initialize_patches();
    
    // Initialize active_patch pointers to point to patch_storage
    for (int i = 0; i < 16; i++) {
        active_patch[i].store(&patch_storage[i], std::memory_order_release);
    }

}

// Add this method to your Synth class:
void Synth::initialize_patches() {
    // Define base wavetables for each channel (cycling through the 5 wave types)
    WaveType channel_base_waves[16] = {
        Sine,     // Channel 0
        Square,   // Channel 1  
        Triangle, // Channel 2
        Sawtooth, // Channel 3
        Sinc,     // Channel 4
        Sine,     // Channel 5
        Square,   // Channel 6
        Triangle, // Channel 7
        Sawtooth, // Channel 8
        Sinc,     // Channel 9
        Sine,     // Channel 10
        Square,   // Channel 11
        Triangle, // Channel 12
        Sawtooth, // Channel 13
        Sinc,     // Channel 14
        Sine      // Channel 15
    };
    
    for (int ch = 0; ch < 16; ch++) {
        Patch& patch = patch_storage[ch];
        
        // Initialize global patch settings
        patch.algorithm = 0;  // Simple FM: op[1] modulates op[0]
        patch.volume = 100;   // Nice default volume
        patch.pan = 64;       // Center pan
        
        // OP[0] - CARRIER (the one we hear)
        patch.ops[0].wave_type = channel_base_waves[ch];  // Different wave per channel
        patch.ops[0].attack = 5;     // Quick attack
        patch.ops[0].decay = 20;     // Medium decay
        patch.ops[0].sustain = 100;  // High sustain
        patch.ops[0].release = 30;   // Medium release
        patch.ops[0].ratio = 1;      // 1:1 ratio (fundamental frequency)
        patch.ops[0].feedback = 0;   // No feedback on carrier
        patch.ops[0].fm_depth = 0;   // Carrier doesn't modulate anything
        
        // OP[1] - MODULATOR (modulates the carrier)
        patch.ops[1].wave_type = Sine;  // Sine wave modulator (classic FM)
        patch.ops[1].attack = 5;     // Quick attack
        patch.ops[1].decay = 15;     // Slightly faster decay than carrier
        patch.ops[1].sustain = 80;   // Medium sustain
        patch.ops[1].release = 25;   // Slightly faster release
        patch.ops[1].ratio = 2;      // 2:1 ratio (one octave higher)
        patch.ops[1].feedback = 0;   // No feedback
        patch.ops[1].fm_depth = 50;  // Medium FM depth
        
        // printf("Initialized patch for channel %d: carrier=%s, modulator=Sine, fm_depth=%d\n", 
        //        ch, wave_type_names[channel_base_waves[ch]], patch.ops[1].fm_depth);
    }
}

void Synth::out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer) {

    // cleanup voices
    for (int i = 0; i < NUM_VOICES; i++) {
        if (voice[i].playing && voice[i].is_idle()) {
            voice[i].playing = false;
            voice[i].steal = false;
        }
    }

    // Apply patches if dirty for any channel
    for (uint8_t ch = 0; ch < 16; ++ch) {
        if (patch_dirty_flags.test(ch)) {
            const Patch *p = active_patch[ch].load(std::memory_order_acquire);
            if (p) {
                for (auto &v : voice) {
                    if (v.playing && v.midi_channel == ch) {
                        v.apply_patch(*p);
                    }
                }
            }
            patch_dirty_flags.reset(ch);
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
        voice[i].out(flow_buffer);
        for (int k = 0; k < SAMPLES_PER_BUFFER; k++) {
            // divide by 8
            buffer[k] += flow_buffer[k] >> 4;
        }
    }

    // The delay effect
    delay_effect.process(buffer.data(), SAMPLES_PER_BUFFER);

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

        // switch (note) {
        //     // case 73: // Attack
        //     //     channel_params[channel].attack = velocity;
        //     //     printf("attack changed: %d\n", velocity);
        //     //     for (int i = 0; i < NUM_VOICES; i++) {
        //     //         if (midi_channel[i] == channel) {
        //     //             envelopes[i].set_ADSR(channel_params[channel].attack,
        //     //                                   channel_params[channel].decay,
        //     //                                   channel_params[channel].sustain
        //     //                                   >> 8,
        //     //                                   channel_params[channel].release);
        //     //         }
        //     //     }
        //     //     break;
        //     // case 75: // Decay
        //     //     channel_params[channel].decay = velocity;
        //     //     printf("decay changed: %d\n", velocity);
        //     //     for (int i = 0; i < NUM_VOICES; i++) {
        //     //         if (midi_channel[i] == channel) {
        //     //             envelopes[i].set_ADSR(channel_params[channel].attack,
        //     //                                   channel_params[channel].decay,
        //     //                                   channel_params[channel].sustain
        //     //                                   >> 8,
        //     //                                   channel_params[channel].release);
        //     //         }
        //     //     }
        //     //     break;
        //     // case 70: // Sustain
        //     //     channel_params[channel].sustain = velocity << 8;
        //     //     printf("sustain changed: %d\n", velocity);
        //     //     for (int i = 0; i < NUM_VOICES; i++) {
        //     //         if (midi_channel[i] == channel) {
        //     //             envelopes[i].set_ADSR(channel_params[channel].attack,
        //     //                                   channel_params[channel].decay,
        //     //                                   channel_params[channel].sustain
        //     //                                   >> 8,
        //     //                                   channel_params[channel].release);
        //     //         }
        //     //     }
        //     //     break;
        //     // case 72: // Release
        //     //     channel_params[channel].release = velocity;
        //     //     printf("release changed: %d\n", velocity);
        //     //     for (int i = 0; i < NUM_VOICES; i++) {
        //     //         if (midi_channel[i] == channel) {
        //     //             envelopes[i].set_ADSR(channel_params[channel].attack,
        //     //                                   channel_params[channel].decay,
        //     //                                   channel_params[channel].sustain
        //     //                                   >> 8,
        //     //                                   channel_params[channel].release);
        //     //         }
        //     //     }
        //     //     break;
        //
        // // case 16: // Filter Cutoff MSB (CC 16)
        // //     channel_params[channel].filter_cutoff_msb = velocity;
        // //     update_filter_cutoff(channel);
        // //     break;
        // // case 48: // Filter Cutoff LSB (CC 48)
        // //     channel_params[channel].filter_cutoff_lsb = velocity;
        // //     update_filter_cutoff(channel);
        // //     break;
        // // case 17: // Filter Resonance/Q MSB (CC 17)
        // //     channel_params[channel].filter_q_msb = velocity;
        // //     update_filter_q(channel);
        // //     break;
        // // case 49: // Filter Resonance/Q LSB (CC 49)
        // //     channel_params[channel].filter_q_lsb = velocity;
        // //     update_filter_q(channel);
        // //     break;
        // // case 18: // Filter Type Selection (CC 18)
        // //     set_filter_type(velocity);
        // //     printf("Filter type changed: %d\n", velocity);
        // //     break;
        // }
        // break;
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
    // Check if note is already playing on this channel
    for (int i = 0; i < NUM_VOICES; i++) {
        if (voice[i].midi_note == note && voice[i].playing && !voice[i].steal &&
            voice[i].midi_channel == channel) {
            printf("Note already playing: note=%d, velocity=%d\n", note,
                   velocity);
            return; // Already playing, no retrigger for now
        }
    }

    // Find a free voice
    for (int i = 0; i < NUM_VOICES; i++) {
        if (!voice[i].playing) {
            // Check if voice belonged to a different channel before
            // bool channel_changed = (voice[i].midi_channel != channel);

            voice[i].midi_channel = channel;
            voice[i].playing = true;
            voice[i].midi_note = note;
            notes_playing_bitset.set(note);
            
            // if (channel_changed) {
                const Patch *patch_ptr =
                    active_patch[channel].load(std::memory_order_acquire);
                if (patch_ptr) {
                    voice[i].apply_patch(*patch_ptr);
                }
            // }
            

            // WaveType wt = channel_wave_map[channel];
            // voice[i].op[1].osc.set_wavetable(wt);


            voice[i].gate_on();
            break;
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
