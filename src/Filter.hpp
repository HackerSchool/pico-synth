#ifndef FILTER_HPP
#define FILTER_HPP

#include "Wavetable.hpp"
#include "config.hpp"
#include "fixed_point.h"
#include "tusb.h"
#include <cstddef>
#include <cstdint>

#define N_Cheb 8
#define m N_Cheb / 2

enum FilterType {
    FILTER_OFF,
    FILTER_LOW_PASS,
    FILTER_CHEBYSHEV,
    NUM_FILTER_TYPES
};

template <typename T> inline void reverse_array(T *array, size_t size) {
    if (size <= 1)
        return; // Nothing to reverse

    size_t start = 0;
    size_t end = size - 1;

    // Swap elements from outside in until we meet in the middle
    while (start < end) {
        // Swap without using a temporary variable when possible
        if constexpr (std::is_integral<T>::value && sizeof(T) <= sizeof(int)) {
            // XOR swap for simple types (integers)
            array[start] ^= array[end];
            array[end] ^= array[start];
            array[start] ^= array[end];
        } else {
            // Standard swap for other types
            T temp = array[start];
            array[start] = array[end];
            array[end] = temp;
        }

        ++start;
        --end;
    }
}

class FilterFIR {
  public:
    FilterFIR(float freq_c);
    FilterFIR(FilterFIR &&) = default;
    FilterFIR(const FilterFIR &) = default;
    FilterFIR &operator=(FilterFIR &&) = default;
    FilterFIR &operator=(const FilterFIR &) = default;
    ~FilterFIR() = default;

    void set_cutoff_freq(float freq_c);
    float get_cutoff(){return q16_to_float(cutoff_freq);}

    // Process a single sample
    int16_t process(int16_t sample);
    void out(int16_t *samples, size_t size);

    // Process a chunk of samples at once
    // std::vector<int16_t> processChunk(const std::vector<int16_t>& input);

    // Process array of samples in-place (more efficient)
    void processChunkInPlace(int16_t *samples, size_t size);

    std::array<int16_t, SAMPLES_PER_BUFFER> &get_output();

    void reset();
    void recalculate_coefficients();
    std::array<q8_24_t, FILTER_ORDER> h = {0};       // filter coefficients
    std::array<int16_t, FILTER_ORDER> h_q2_14 = {0}; // filter coefficients

  private:
    // Filter state
    std::array<int16_t, FILTER_ORDER> buffer = {0};
    int buffer_index = 0;
    std::array<int16_t, SAMPLES_PER_BUFFER> output = {};
    std::array<int16_t, SAMPLES_PER_BUFFER> *in_signal;

    q16_16_t cutoff_freq;
    q8_24_t fc_norm; // normalized cutoff frequency
};

class FilterCheb {
  public:
    FilterCheb(float fc, float epsilon, float fs);
    FilterCheb(FilterCheb &&) = default;
    FilterCheb(const FilterCheb &) = default;
    FilterCheb &operator=(FilterCheb &&) = default;
    FilterCheb &operator=(const FilterCheb &) = default;
    ~FilterCheb() = default;

    void set_cutoff_freq(float fc, float epsilon);
    float get_cutoff() { return q16_to_float(cutoff_freq); }
    // Process a single sample
    int16_t process(int16_t sample);
    void out(int16_t *samples, size_t size);

    // Process a chunk of samples at once
    // std::vector<int16_t> processChunk(const std::vector<int16_t>& input);

    // Process array of samples in-place (more efficient)
    void processChunkInPlace(int16_t *samples, size_t size);
    int16_t che_low_pass(int16_t x);

    std::array<int16_t, SAMPLES_PER_BUFFER> &get_output();

    void reset();
    void recalculate_coefficients();

  private:
    // Filter state
    std::array<int16_t, SAMPLES_PER_BUFFER> output = {};
    std::array<int16_t, SAMPLES_PER_BUFFER> *in_signal;

    q16_16_t cutoff_freq;
    q8_24_t fc_norm; // normalized cutoff frequency
    //
    int16_t A[m] = {0};
    int16_t d1[m] = {0};
    int16_t d2[m] = {0};
    int16_t w0[m] = {0};
    int16_t w1[m] = {0};
    int16_t w2[m] = {0};
};

#endif // FILTER_HPP
