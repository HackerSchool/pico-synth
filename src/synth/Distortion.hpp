// Distortion.hpp
#ifndef DISTORTION_HPP
#define DISTORTION_HPP

#include <cstdint>

class Distortion {
public:
    Distortion();
    void set_params(int drive_q15, int thresh, int mix_q15);
    void process(int16_t* buffer, int buffer_size);
    void reset();

private:
    int input_gain_q12;
    int clip_threshold;
    int clip_threshold_inv_q15;
    int makeup_gain_q12;
    int mix_q15;
    int tone_alpha_q15;
    int32_t lp_state;
    int32_t hp_prev_in;
    int32_t hp_prev_out;
};

#endif // DISTORTION_HPP
