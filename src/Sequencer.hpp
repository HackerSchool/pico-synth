#ifndef STEP_SEQUENCER
#define STEP_SEQUENCER

#include "tusb.h"
#include "MidiHandler.hpp"
#include "Synth.hpp"
#include "config.hpp"
#include <cstdint>

#define SEQ_LEN 16

typedef struct {
    uint8_t notes[8]; // we use 200 as unassigned value
} StepInfo;

class Sequencer {
  public:
    Sequencer(Synth &synth, MidiHandler &midi);

    void initialize_pattern();
    void update();

    void play();
    void pause();
    void play_from_step(uint8_t step);

    void play_step(uint8_t step);
    void add_note_to_step(uint8_t step, uint8_t note);
    void remove_note_from_step(uint8_t step, uint8_t note);

    void set_tempo(uint32_t tempo);
    void set_swing(uint32_t swing);

    void get_notes_on_step(uint8_t step);
    void get_notes();
    void get_tempo();
    void get_swing();

  private:
    Synth &synth;
    MidiHandler &midi;

    StepInfo step_info[SEQ_LEN];

    uint32_t step_counter = 0;
    uint8_t step_current = 0;

    uint32_t tempo = 120;
    uint32_t swing = 0;
    uint32_t cycle_count = 100;
    float cycle_freq = 44100.f * 150.f / (SAMPLES_PER_BUFFER * 96.f);
};

#endif // !STEP_SEQUENCER
