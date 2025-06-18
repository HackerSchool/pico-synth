#include "Operator.hpp"
#include "config.hpp"
#include <cstdint>

Operator::Operator() {
    osc = Oscillator(Sine, 440.f);
    env = ADSREnvelope(1, 1, 100, 1);
    env.set_ADSR(5, 5, 100, 5);
}

void Operator::out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer,
                   uint8_t fm_depth_) {
    osc.out_interp(buffer, fm_depth_);
    env.out(buffer);
}



//Voice

Voice::Voice() {
    for(int i = 0; i < OP_PER_VOICE; i++){
        op[i] = Operator();
    }
}

void Voice::out(std::array<int16_t, SAMPLES_PER_BUFFER> &buffer,
                uint8_t fm_depth_) {

    op[0].out(buffer, 0);
    op[1].out(buffer, fm_depth_);
}


void Voice::gate_on(){
    for(int i = 0; i < OP_PER_VOICE; i++){
        op[i].env.gate_on();
    }
}

void Voice::gate_off(){
    for(int i = 0; i < OP_PER_VOICE; i++){
        op[i].env.gate_off();
    }
}

