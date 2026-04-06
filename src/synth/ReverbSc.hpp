#ifndef REVERBSC_HPP
#define REVERBSC_HPP

#include <array>
#include <cstddef>
#include <cstdint>

class ReverbScFx {
public:
    ReverbScFx();

    // time: 0.0 .. 1.0
    // tone: 0.0 .. 1.0 (dark .. bright)
    // mix:  0.0 .. 1.0
    void set_params(float time, float tone, float mix);
    void process(int16_t* buffer, int buffer_size);
    void reset();

private:
    static constexpr size_t NUM_DIFFUSERS = 4;
    static constexpr size_t NUM_LINES = 8;

    static constexpr size_t DIFFUSER_BUF_SIZE = 256;
    static constexpr size_t LINE_BUF_SIZE = 2048;

    static constexpr size_t DIFFUSER_MASK = DIFFUSER_BUF_SIZE - 1;
    static constexpr size_t LINE_MASK = LINE_BUF_SIZE - 1;

    static constexpr size_t DIFFUSER_DELAYS[NUM_DIFFUSERS] = {43, 59, 83, 109};
    static constexpr size_t LINE_BASE_DELAYS[NUM_LINES] = {
        353, 463, 631, 797, 941, 1097, 1279, 1571
    };
    static constexpr int8_t INPUT_POLARITY[NUM_LINES] = {
        1, 1, -1, -1, 1, -1, 1, -1
    };
    static constexpr int16_t MOD_DEPTH_OFFSETS[NUM_LINES] = {
        0, 1, 2, 0, 3, 1, 2, 4
    };

    struct Allpass {
        int16_t* buf;
        size_t idx;
        size_t delay;
        size_t mask;
        int32_t gain_q15;
    };

    struct DelayLine {
        int16_t* buf;
        size_t idx;
        size_t delay;
        int32_t filterstore;
        uint32_t mod_phase;
        uint32_t mod_rate;
        int16_t mod_depth;
    };

    std::array<std::array<int16_t, DIFFUSER_BUF_SIZE>, NUM_DIFFUSERS> diffuser_bufs{};
    std::array<std::array<int16_t, LINE_BUF_SIZE>, NUM_LINES> line_bufs{};
    std::array<Allpass, NUM_DIFFUSERS> diffusers{};
    std::array<DelayLine, NUM_LINES> lines{};

    int32_t feedback_q15 = 0;
    int32_t tone_q15 = 0;
    int32_t mix_q15 = 0;

    static inline int16_t clamp16(int32_t x);
    static inline int32_t process_allpass(Allpass& ap, int32_t x);
    static inline int32_t triangle_q15(uint32_t phase);
};

#endif
