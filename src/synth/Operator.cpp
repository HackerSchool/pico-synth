#include "Operator.hpp"
#include "Envelope.hpp"
#include "config.hpp"
#include <cstdint>
#include <tusb.h>

Operator::Operator() {
    osc = Oscillator(Sine, 440.f);
    env = ADSREnvelope(1, 1, 100, 1);
    env.set_ADSR(5, 5, 100, 5);
}

void Operator::out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer,
                   uint16_t fm_depth_) {
    osc.out_interp(buffer, fm_depth_);
    env.out(buffer);
}

// Voice

Voice::Voice() {
    for (int i = 0; i < OP_PER_VOICE; i++) {
        op[i] = Operator();
    }
    op[0].env.set_ADSR(0, 80, 5, 100);
}

void Voice::out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer) {

    op[1].out(buffer, 0);
    op[0].out(buffer, op[1].fm_depth);
}

void Voice::gate_on() {
    for (int i = 0; i < OP_PER_VOICE; i++) {
        op[i].env.gate_on();
    }
}

void Voice::gate_off() {
    for (int i = 0; i < OP_PER_VOICE; i++) {
        op[i].env.gate_off();
    }
}

bool Voice::is_idle() { return (op[0].env.state == ADSREnvelope::ENV_IDLE); }

// Enhanced apply_patch with more debugging:
void Voice::apply_patch(const Patch &patch) {
    printf("=== Applying patch to voice - channel: %d, note: %d ===\n",
           midi_channel, midi_note);

    for (int i = 0; i < OP_PER_VOICE; ++i) {
        const OperatorParams &p = patch.ops[i];

        printf("  Op %d BEFORE: fm_depth=%d, ratio=%d\n", i, op[i].fm_depth,
               op[i].ratio);

        // Apply patch parameters
        op[i].osc.set_wavetable(p.wave_type);
        op[i].env.set_ADSR(p.attack, p.decay, p.sustain, p.release);
        op[i].ratio = p.ratio;
        op[i].feedback = p.feedback;
        op[i].fm_depth = p.fm_depth;

        printf("  Op %d AFTER: fm_depth=%d, ratio=%d\n", i, op[i].fm_depth,
               op[i].ratio);
    }
    printf("=== Patch applied ===\n");
}
