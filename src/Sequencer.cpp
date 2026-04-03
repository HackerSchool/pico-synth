#include "Sequencer.hpp"
#include "Sampler.hpp"
#include "Ui.hpp"
#include <cstdint>

Sequencer::Sequencer(MidiHandler &midi, Sampler &sampler)
    : midi(midi), sampler(sampler) {
    set_tempo(120);
    initialize_pattern();
    printf("\n\n\nSequencer init!\n\n\n");
    schedule_next_alarm();
}

void Sequencer::update() {

    // the step flag is set by a hardware timer!
    if (play_flag && step_flag) {
        // printf("Step flag set yo!\n");
        play_step(step_current);
        step_current++;
        if (step_current >= SEQ_LEN) {
            step_current = 0;
        }
        step_flag = 0;
    }
}

void Sequencer::schedule_next_alarm() {
    // uint64_t delay_us =
    //     toggle ? 2000000 : 1000000; // Alternate between 2s and 1s
    toggle = !toggle;
    // uint64_t delay_us = timer_interval;
    add_alarm_in_us(timer_interval, alarm_callback, this, true);
}

int64_t Sequencer::alarm_callback(alarm_id_t id, void *user_data) {
    Sequencer *self = static_cast<Sequencer *>(user_data);
    self->step_flag = true;      // Set the flag
    self->schedule_next_alarm(); // Chain the next alarm
    return 0;
}

void Sequencer::initialize_pattern() {
    // const uint8_t arp_notes[] = {60, 64, 67,
    //                              72}; // C major arpeggio: C4, E4, G4, C5

    for (uint8_t step = 0; step < SEQ_LEN; step++) {
        for (uint8_t i = 0; i < 8; ++i) {
            step_info[step].notes[i] = UNASSIGNED;
            step_info[step].channel[i] = 1;
        }

        // // Use one note per step from arpeggio, cycling through it
        // step_info[step].notes[0] = arp_notes[step % 4] - 12;
        //
        // // For a fancier feel, add a high octave every 4 steps
        // if (step % 4 == 0) {
        //     step_info[step].notes[1] = arp_notes[step % 4] + 24;
        // }
    }
}

void Sequencer::play() { play_flag = true; }
void Sequencer::pause() {
    play_flag = false;
    uint8_t prev_step = (step_current == 0 ? SEQ_LEN - 1 : step_current - 1);

    for (int note_id = 0; note_id < NOTES_PER_STEP; note_id++) {
        uint8_t note = step_info[prev_step].notes[note_id];
        uint8_t channel = step_info[prev_step].channel[note_id];
        if (note != UNASSIGNED) {
            uint8_t packet[4];
            packet[0] = 0x08;                    // CIN = Note Off, Cable 0
            packet[1] = 0x80 | (channel & 0x0F); // Status
            packet[2] = note;
            packet[3] = 0x7F; // Velocity
            midi.midi_receive_note(packet);
        }
    }
}
void Sequencer::play_from_step(uint8_t step) {}

void Sequencer::play_step(uint8_t step) {
    if (step >= SEQ_LEN) {
        fprintf(stderr, "Error: step %d out of range (max %d)\n", step,
                SEQ_LEN - 1);
        return;
    }

    uint8_t prev_step = (step == 0 ? SEQ_LEN - 1 : step - 1);

    for (int note_id = 0; note_id < NOTES_PER_STEP; note_id++) {
        uint8_t note = step_info[prev_step].notes[note_id];
        uint8_t channel = step_info[prev_step].channel[note_id];
        if (note != UNASSIGNED) {

            uint8_t packet[4];
            packet[0] = 0x08;                    // CIN = Note Off, Cable 0
            packet[1] = 0x80 | (channel & 0x0F); // Status
            packet[2] = note;
            packet[3] = 0x7F; // Velocity
            midi.midi_receive_note(packet);
        }
    }

    for (int note_id = 0; note_id < NOTES_PER_STEP; note_id++) {
        uint8_t note = step_info[step].notes[note_id];
        uint8_t channel = step_info[step].channel[note_id];
        if (note != UNASSIGNED) {
            if (channel == 5) {
                sampler.trigger_player(note - 60);
            } else {
                uint8_t packet[4];
                packet[0] = 0x09;                    // CIN = Note On, Cable 0
                packet[1] = 0x90 | (channel & 0x0F); // Status
                packet[2] = note;
                packet[3] = 0x7F; // Velocity
                midi.midi_receive_note(packet);
            }
        }
    }
}
void Sequencer::add_note_to_step(uint8_t step, uint8_t note) {}
void Sequencer::remove_note_from_step(uint8_t step, uint8_t note) {}

void Sequencer::toggle_note_step(uint8_t step_, uint8_t note_,
                                 uint8_t channel_) {

    bool note_already_in = false;
    printf("Note: %d step: %d channel %d\n", note_, step_, channel_);
    // see if the note is already in the step, in which case we take it out
    for (int note_id = 0; note_id < NOTES_PER_STEP; note_id++) {
        uint8_t note = step_info[step_].notes[note_id];
        uint8_t channel = step_info[step_].channel[note_id];
        if (note == note_ && channel == channel_) {
            step_info[step_].notes[note_id] = UNASSIGNED;
            step_info[step_].channel[note_id] = 0;
            note_already_in = true;
        }
    }
    if (!note_already_in) {

        for (int note_id = 0; note_id < NOTES_PER_STEP; note_id++) {
            uint8_t note = step_info[step_].notes[note_id];
            if (note == UNASSIGNED) {
                step_info[step_].notes[note_id] = note_;
                step_info[step_].channel[note_id] = channel_;
                break;
            }
        }
    }
}

void Sequencer::set_tempo(uint32_t new_tempo) {
    tempo = new_tempo;
    // Use integer arithmetic to avoid floating-point on Cortex-M0+
    // interval in microseconds per beat = 60,000,000 / tempo
    if (tempo == 0) tempo = 120; // guard against divide-by-zero
    timer_interval = 60000000ULL / tempo;
};

void Sequencer::set_swing(uint32_t swing) {}

void Sequencer::get_notes_on_step(uint8_t step) {}
void Sequencer::get_notes() {}
void Sequencer::get_tempo() {}
void Sequencer::get_swing() {}

uint8_t Sequencer::get_current_step() { return step_current; }

// Helper function to check if sequencer is playing
bool Sequencer::is_playing() { return play_flag; }

// Get array of notes for a specific step
uint8_t *Sequencer::get_step_notes(uint8_t step) {
    if (step >= SEQ_LEN)
        return nullptr;
    return step_info[step].notes;
}

// Get array of channels for a specific step
uint8_t *Sequencer::get_step_channels(uint8_t step) {
    if (step >= SEQ_LEN)
        return nullptr;
    return step_info[step].channel;
}

// Count how many notes are active on a step
uint8_t Sequencer::count_notes_on_step(uint8_t step) {
    if (step >= SEQ_LEN)
        return 0;
    uint8_t count = 0;
    for (int i = 0; i < NOTES_PER_STEP; i++) {
        if (step_info[step].notes[i] != UNASSIGNED) {
            count++;
        }
    }
    return count;
}
