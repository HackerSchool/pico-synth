// Chorus.hpp
#ifndef CHORUS_HPP
#define CHORUS_HPP

#include <array>
#include <cstdint>
#include "config.hpp"

class Chorus {
public:
    Chorus();
    ~Chorus();
    void set_params(int rate_unused, int depth, int mix_q15);
    void process(int16_t* buffer, int buffer_size);
    void reset();

private:
    static const size_t MAX_CHORUS_SAMPLES = 4096;
    std::array<int16_t, MAX_CHORUS_SAMPLES> buf;
    size_t write_idx;
    int delay_samples;
    int mix_q15;
};

#endif // CHORUS_HPP
