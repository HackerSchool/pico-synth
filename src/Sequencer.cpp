#include "Sequencer.hpp"
#include "Ui.hpp"
#include <cstdint>

Sequencer::Sequencer(Synth &synth, MidiHandler &midi)
    : synth(synth), midi(midi) {
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
    Sequencer* self = static_cast<Sequencer*>(user_data);
    self->step_flag = true;         // Set the flag
    self->schedule_next_alarm();    // Chain the next alarm
    return 0;
}


void Sequencer::initialize_pattern() {
    const uint8_t UNASSIGNED = 200;
    const uint8_t arp_notes[] = { 60, 64, 67, 72 }; // C major arpeggio: C4, E4, G4, C5

    for (uint8_t step = 0; step < SEQ_LEN; step++) {
        for (uint8_t i = 0; i < 8; ++i) {
            step_info[step].notes[i] = UNASSIGNED;
        }

        // Use one note per step from arpeggio, cycling through it
        step_info[step].notes[0] = arp_notes[step % 4] - 12;

        // For a fancier feel, add a high octave every 4 steps
        if (step % 4 == 0) {
            step_info[step].notes[1] = arp_notes[step % 4] + 24;
        }
    }
}

void Sequencer::play(){
    play_flag = true;
}
void Sequencer::pause(){
    play_flag = false;
}
void Sequencer::play_from_step(uint8_t step){}

void Sequencer::play_step(uint8_t step) {
    // printf("Playing step: %d\n", step);

    uint8_t prev_step = (step == 0 ? SEQ_LEN - 1 : step - 1);
    for(int note_id = 0; note_id < 8; note_id++){
        uint8_t note = step_info[prev_step].notes[note_id];
        if (note != 200)
            synth.note_off(note, 127);
    }

    for(int note_id = 0; note_id < 8; note_id++){
        uint8_t note = step_info[step].notes[note_id];
        if (note != 200)
            synth.note_on(note, 127);
    }
}
void Sequencer::add_note_to_step(uint8_t step, uint8_t note){}
void Sequencer::remove_note_from_step(uint8_t step, uint8_t note){}

void Sequencer::set_tempo(uint32_t new_tempo) {
    tempo = new_tempo;
    float interval = 60.f * 1000000.f / tempo;
    timer_interval = static_cast<uint64_t>(interval);
};

void Sequencer::set_swing(uint32_t swing){}

void Sequencer::get_notes_on_step(uint8_t step){}
void Sequencer::get_notes(){}
void Sequencer::get_tempo(){}
void Sequencer::get_swing(){}


uint8_t Sequencer::get_current_step(){
    return step_current;
}
