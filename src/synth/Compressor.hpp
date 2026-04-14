#ifndef COMPRESSOR_HPP
#define COMPRESSOR_HPP

#include <cstdint>

class Compressor {
public:
    Compressor();
    void set_params(int threshold, int makeup, int mix_q15);
    void process(int16_t *buffer, int buffer_size);
    void reset();

private:
    int32_t threshold_level = 12000;
    int32_t makeup_gain_q12 = 4096;
    int32_t mix_q15 = 32767;
    int32_t envelope = 0;
};

#endif // COMPRESSOR_HPP
