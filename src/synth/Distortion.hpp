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
    int drive_q15; // simple drive parameter (Q15-ish)
    int16_t thresh; // clipping threshold
    int mix_q15; // wet mix in Q15
};

#endif // DISTORTION_HPP
