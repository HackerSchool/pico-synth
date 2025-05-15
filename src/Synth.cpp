#include "Synth.hpp"
#include "Oscillator.hpp"
#include "Wavetable.hpp"
#include "config.hpp"
#include <cstdint>
#include <cstdio>

Synth::Synth() {
    // init the oscillators and envelopes
    for (int i = 0; i < NUM_OSC; i++) {
        oscillators[i] = Oscillator(Sawtooth, 440.f);
        envelopes[i] = ADSREnvelope(0.1f, 0.2f, 0.8f, .5f,
                                    oscillators[i].get_output(), 0.f);
    }
}

void Synth::out() {
    output = {};
    for (int i = 0; i < NUM_OSC; i++) {
        oscillators[i].out();
        envelopes[i].out();
    }
    for (int i = 0; i < NUM_OSC; i++) {
        std::array<int16_t, SAMPLES_PER_BUFFER> &env_out_i = envelopes[i].get_output();
        for (int k = 0; k < SAMPLES_PER_BUFFER; k++) {
            // divide by 8
            output[k] += env_out_i[k] >> 3;
        }
    }

    // low_pass.processChunkInPlace(output.data(), output.size());
    // low_pass.out(output.data(), output.size());
    low_pass_cheb.out(output.data(), output.size());
}

std::array<int16_t, SAMPLES_PER_BUFFER> &Synth::get_output() { return output; }

void Synth::process_midi_packet(uint8_t packet[4]) {
    uint8_t msg_type = packet[1] & 0xF0;
    // uint8_t channel = packet[1] & 0x0F;
    uint8_t note = packet[2];
    uint8_t velocity = packet[3];

    switch (msg_type) {
    case 0x90: // Note On
        if (velocity > 0) {
            // Note on with velocity
            // printf("Note On: channel=%d, note=%d, velocity=%d\n", channel,
            // note,
            //        velocity);
            note_on(note, velocity);
        } else {
            // Note on with velocity 0 is equivalent to Note Off
            // printf("Note Off (via Note On): channel=%d, note=%d\n", channel,
            // note);
            note_off(note, velocity);
        }
        break;

    case 0x80: // Note Off
        // printf("Note Off: channel=%d, note=%d, velocity=%d\n",
        // channel, note,
        // velocity);

        note_off(note, velocity);
        break;

    case 0xB0: // Control Change
        // printf("Control Change: channel=%d, controller=%d, value = % d\n
        // ",channel channel, note, velocity); Handle control change message
        if (note == 0x02) {
            float fc = 200.f + (float)velocity / 127.f * 9000.f;
            // printf("fc = %f\n", fc);
            // low_pass.set_cutoff_freq(fc);
            low_pass_cheb.set_cutoff_freq(fc, 0.5f);
        }
        // For example: set_controller(note, velocity);
        //
        break;

        // Add other MIDI message types as needed
    }
}

void Synth::note_on(uint8_t note, uint8_t velocity) {
    // Check if note is playing
    int osc_index = -1;
    for (int i = 0; i < NUM_OSC; i++) {
        if (osc_midi_note[i] == note && osc_playing[i]) {
            // printf("Note already playing: note=%d, velocity=%d\n", note,
            // velocity);
            osc_index = i;
            break;
        }
    }

    // Check if there are any free oscilators
    if (osc_index == -1) {
        for (int i = 0; i < NUM_OSC; i++) {
            if (!osc_playing[i]) {
                osc_playing[i] = true;
                osc_midi_note[i] = note;
                notes_playing_bitset.set(note);

                float freq = midi_to_freq(note);
                oscillators[i].set_freq(freq);
                envelopes[i].set_trigger(5.f);
                envelopes[i].set_idle();
                // printf("New Note: note=%d, velocity=%d\n", note, velocity);
                break;
            }
        }
    }
}

void Synth::note_off(uint8_t note, uint8_t velocity) {
    // Check if note is playing
    // printf("I am in note off");
    for (int i = 0; i < NUM_OSC; i++) {
        if (osc_midi_note[i] == note && osc_playing[i]) {
            envelopes[i].set_trigger(0.f);
            osc_playing[i] = false;
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
