#include "Filter.hpp"
#include "Wavetable.hpp"
#include "fixed_point.h"
#include "tusb.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>

FilterFIR::FilterFIR(float freq_c) {
    set_cutoff_freq(freq_c);
}

void FilterFIR::set_cutoff_freq(float freq_c) {

    cutoff_freq = q16_from_float(freq_c);
    // fc_norm = q16_to_q24(q16_div(cutoff_freq, fs) << 1);
    fc_norm = q24_from_float(freq_c / (44100.f / 2));
    printf("%f\n", freq_c);

    // Calculate filter coefficients
    recalculate_coefficients();
}

void FilterFIR::recalculate_coefficients() {
    // Use pre-calculated hanning window
    // If FILTER_ORDER is the same as hanning_window_table_fp.size(),
    // we can use the values directly

    // Normalize coefficients to ensure unity gain at DC
    q8_24_t sum = q24_from_int(0);
    q8_24_t M = q24_from_int(FILTER_ORDER - 1) >> 1;
    for (int i = 0; i < FILTER_ORDER; i++) {
        // Calculate sinc value using pre-calculated table
        q8_24_t n_minus_M = q24_sub(q24_from_int(i), M);
        // q8_24_t x = q24_mul(q24_mul(q24_from_int(2), fc_norm), n_minus_M);
        q8_24_t x = q24_mul(fc_norm, n_minus_M) << 1;

        // Take absolute value of x (sinc is symmetric)
        if (x < 0)
            x = -x;

        // Map x to the sinc_table_fp index range
        // q8_24_t index_float = q24_div(q24_mul(x, q24_from_int(WAVE_TABLE_LEN
        // - 1)),
        //                               q24_from_int(32));

        int64_t index_float = static_cast<int64_t>(x)
                              << 4; // multibly by 16 -> sinc_len/sinc_range
                                    // this can overflow! now in Q40.24

        // Linear interpolation for sinc value from pre-calculated table
        int index_low = static_cast<int>(index_float >> 24);
        int index_high = index_low + 1;
        if (index_high >= WAVE_TABLE_LEN)
            index_high = WAVE_TABLE_LEN - 1;

        // Get sinc values from table
        q8_24_t sinc_val_low = sinc_table_fp[index_low];
        q8_24_t sinc_val_high = sinc_table_fp[index_high];

        // Fractional part for interpolation weight
        // q8_24_t weight = q24_sub(index_float, q24_from_int(index_low));
        // the 24 least significant bits from index_float
        q8_24_t weight = (index_float & 0xFFFFFF);
        q8_24_t one_minus_weight = q24_sub(q24_from_int(1), weight);

        q8_24_t sinc_val = q24_add(q24_mul(one_minus_weight, sinc_val_low),
                                   q24_mul(weight, sinc_val_high));

        // printf("fc_norm = %f\n\r", q24_to_float(fc_norm));
        // // printf("cutoff = %f\n\r", q16_to_float(cutoff_freq));
        // printf("n-minus-m = %f\n\r", q24_to_float(n_minus_M));
        // printf("M = %ld\n\r", q24_to_int(M));
        // printf("x = %f\n\r", q24_to_float(x));
        // printf("index_float = %f\n\r", q24_to_float(index_float));
        // printf("index_low = %d\n\r", index_low);
        // printf("sinc_val_high = %f\n\r", q24_to_float(sinc_val_high));
        // printf("sinc_val_low = %f\n\r", q24_to_float(sinc_val_low));
        // printf("weight = %f\n\r", q24_to_float(weight));
        // printf("sinc_val = %f\n\r", q24_to_float(sinc_val));

        // Apply window function from pre-calculated table
        h[i] = q24_mul(sinc_val, hanning_window_table_fp[i]);
        h_q2_14[i] = (int16_t)(h[i] >> 10); // save as 16 bit aswell
        sum = q24_add(sum, h[i]);
    }

    // Normalize coefficients
    if (sum != 0) {
        q8_24_t norm_factor = q24_div(q24_from_int(1), sum);
        for (int i = 0; i < FILTER_ORDER; i++) {
            h[i] = q24_mul(h[i], norm_factor);
        }
    }
    reverse_array(h.data(), h.size());
}

void FilterFIR::reset() {
    for (int i = 0; i < FILTER_ORDER; i++) {
        buffer[i] = 0;
    }
    buffer_index = 0;
}

void FilterFIR::out(int16_t *samples, size_t size) {
    for (size_t i = 0; i < size; i++) {
        buffer[buffer_index] = samples[i];
        int32_t sum = 0;
        for (size_t j = 0; j < FILTER_ORDER; j++) {
            int sampleIndex = (buffer_index - j + FILTER_ORDER) % FILTER_ORDER;
            sum += (int32_t)(buffer[sampleIndex] * h_q2_14[j]);
        }
        // Update buffer index for next sample
        buffer_index = (buffer_index + 1) % FILTER_ORDER;
        samples[i] =
            (int16_t)(sum >> (5 + 14)); // scale due to number of filter
    }
}

std::array<int16_t, 1156> &FilterFIR::get_output() { return output; }

int16_t FilterFIR::process(int16_t sample) {
    // Store new sample in circular buffer
    buffer[buffer_index] = sample;

    // Perform convolution
    q8_24_t result = q24_from_int(0);

    for (int i = 0; i < FILTER_ORDER; i++) {
        // Calculate the correct buffer index
        // This accesses newest sample first, then older samples
        int sampleIndex = (buffer_index - i + FILTER_ORDER) % FILTER_ORDER;

        // Get sample from circular buffer
        const int16_t buf_sample = buffer[sampleIndex];

        // Convert to q8.24, multiply with coefficient, and accumulate
        result = q24_add(result, q24_mul(q24_from_int(buf_sample), h[i]));
    }

    // Update buffer index for next sample
    buffer_index = (buffer_index + 1) % FILTER_ORDER;

    // Convert result back to int16_t with proper rounding
    return (int16_t)q24_to_int(result);
}
// More efficient block processing with cache optimization
void FilterFIR::processChunkInPlace(int16_t *samples, size_t size) {
    // Process in smaller chunks to better utilize cache
    constexpr size_t CHUNK_SIZE = 16;

    for (size_t i = 0; i < size; i += CHUNK_SIZE) {
        const size_t chunk_end = std::min(i + CHUNK_SIZE, size);

        // Process this smaller chunk
        for (size_t j = i; j < chunk_end; j++) {
            samples[j] = process(samples[j]);
        }
    }
}
