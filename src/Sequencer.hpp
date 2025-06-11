#ifndef STEP_SEQUENCER
#define STEP_SEQUENCER

#include "MidiHandler.hpp"
#include "Synth.hpp"
#include "config.hpp"
#include "hardware/structs/systick.h"
#include "hardware/timer.h"
#include "tusb.h"
#include <cstdint>

#define SEQ_LEN 16
#define UNASSIGNED 200
#define NOTES_PER_STEP 8

typedef struct {
    uint8_t notes[NOTES_PER_STEP]; // we use 200 as unassigned value
} StepInfo;

class Sequencer {
  public:
    Sequencer(MidiHandler &midi);

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
    uint8_t get_current_step();

    void schedule_next_alarm();

    static int64_t alarm_callback(alarm_id_t id, void *user_data);

  private:
    MidiHandler &midi;

    StepInfo step_info[SEQ_LEN];

    uint8_t step_current = 0;
    uint64_t timer_interval = 1000000;
    uint32_t tempo = 120;
    uint32_t swing = 0;

    bool toggle = 0;
    bool step_flag = 0;

    bool play_flag = 0;
};

#endif // !STEP_SEQUENCER
