#ifndef REVERB_HPP
#define REVERB_HPP

#include <array>
#include <cstddef>
#include <cstdint>

class Reverb {
public:
    Reverb();

    // room_size: 0.0 .. 1.0
    // damp:      0.0 .. 1.0
    // mix:       0.0 .. 1.0
    void set_params(float room_size, float damp, float mix);

    // In-place mono processing
    void process(int16_t* buffer, int buffer_size);

    void reset();

private:
    static constexpr size_t NUM_DIFFUSERS = 3;
    static constexpr size_t NUM_LINES = 4;

    // Power-of-2 buffers keep ring wrapping branchless and cheap.
    static constexpr size_t PRE_DELAY_BUF_SIZE = 2048;
    static constexpr size_t DIFFUSER_BUF_SIZE = 256;
    static constexpr size_t LINE_BUF_SIZE = 4096;

    static constexpr size_t PRE_DELAY_MASK = PRE_DELAY_BUF_SIZE - 1;
    static constexpr size_t DIFFUSER_MASK = DIFFUSER_BUF_SIZE - 1;
    static constexpr size_t LINE_MASK = LINE_BUF_SIZE - 1;

    static constexpr size_t DIFFUSER_DELAYS[NUM_DIFFUSERS] = {
        43, 71, 149
    };

    static constexpr size_t LINE_BASE_DELAYS[NUM_LINES] = {
        907, 1237, 1657, 2143
    };

    static constexpr int16_t LINE_MOD_DEPTH_OFFSETS[NUM_LINES] = {
        0, 2, 4, 1
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

    std::array<int16_t, PRE_DELAY_BUF_SIZE> pre_delay_buf{};
    std::array<int16_t, DIFFUSER_BUF_SIZE> diffuser_buf1{};
    std::array<int16_t, DIFFUSER_BUF_SIZE> diffuser_buf2{};
    std::array<int16_t, DIFFUSER_BUF_SIZE> diffuser_buf3{};

    std::array<int16_t, LINE_BUF_SIZE> line_buf1{};
    std::array<int16_t, LINE_BUF_SIZE> line_buf2{};
    std::array<int16_t, LINE_BUF_SIZE> line_buf3{};
    std::array<int16_t, LINE_BUF_SIZE> line_buf4{};

    size_t pre_delay_idx = 0;
    size_t pre_delay_samples = 0;

    std::array<Allpass, NUM_DIFFUSERS> diffusers{};
    std::array<DelayLine, NUM_LINES> lines{};

    // Q15 params
    int32_t feedback_q15 = 0;
    int32_t damp_q15 = 0;
    int32_t mix_q15 = 0;

    static inline int16_t clamp16(int32_t x);
    static inline int32_t process_allpass(Allpass& ap, int32_t x);
    static inline int32_t triangle_q15(uint32_t phase);
};

#endif
