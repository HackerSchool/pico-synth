#include "Filter.hpp"
#include "Wavetable.hpp"
#include "fixed_point.h"
#include "tusb.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>

FilterFIR::FilterFIR(float freq_c) { set_cutoff_freq(freq_c); }

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
        // the 24 least significant bits from index_float
        q8_24_t weight = (index_float & 0xFFFFFF);
        q8_24_t one_minus_weight = q24_sub(q24_from_int(1), weight);

        q8_24_t sinc_val = q24_add(q24_mul(one_minus_weight, sinc_val_low),
                                   q24_mul(weight, sinc_val_high));

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
            (int16_t)(sum >> (3 + 14)); // scale due to number of filter
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

int16_t
lookup_table_interpolate(const std::array<int16_t, WAVE_TABLE_LEN> &table,
                         int16_t x, size_t q) {
    int16_t index = ((int32_t)x * (WAVE_TABLE_LEN)) >> q; // Scale based on q
    // printf("index %d\n", index);

    int16_t v1 = table[index];
    int16_t v2 = table[index + 1];
    // printf("v1 %f\n", q2_14_to_float(v1));
    // printf("v2 %f\n", q2_14_to_float(v2));

    int16_t frac = (int16_t)((int32_t)x * (WAVE_TABLE_LEN)) -
                   (index << q); // Compute fractional part
    // printf("frac %f\n", q1_15_to_float(frac));
    // Linear interpolation: (1 - frac) * v1 + frac * v2
    return ((int32_t)v1 * ((1 << q) - frac) + (int32_t)v2 * frac) >> q;
}

FilterCheb::FilterCheb(float fc, float epsilon, float fs) {

    // int16_t ep = float_to_q2_14(epsilon); // in ]0.3, 1]
    set_cutoff_freq(fc, epsilon);
}

void FilterCheb::set_cutoff_freq(float fc, float epsilon) {

    int16_t ep = float_to_q2_14(epsilon); // in ]0.3, 1]
    float fs = 44100;
    int16_t fc_norm = float_to_q1_15(fc / fs);

    // printf("fc_norm %f\n", q1_15_to_float(fc_norm));

    // int16_t a = tan(M_PI * fc / fs); //here I can guarantee that a < 1
    // because fc/fs < 0.25, so it can be q1.15 int16_t a = tan(M_PI * fc / fs);
    // //here I can guarantee that a < 1 because fc/fs < 0.25, so it can be
    // q1.15
    //
    int16_t a =
        lookup_table_interpolate(tan_wave_table, fc_norm << 2, 15); // Q1.15

    // printf("a = %f\n", q1_15_to_float(a));

    int16_t a2 = (int32_t)a * a >> 15; // q1.15
    // int16_t u = log((1.0 + sqrt(1.0 + ep * ep)) / ep); //smaller than 2,
    // maybe q2.14 int16_t su = sinh(u / n); // in [0, 2] use a lookup table,
    // maybe from 0 to 1 for resolution int16_t cu = cosh(u / n); // in [0, 2]
    // use a lookup table, maybe from 0 to 1 for resolution
    int16_t u = lookup_table_interpolate(u_wave_table, ep, 14); // Q2.14
    // printf("u %f\n", q2_14_to_float(u));
    // printf("a2 %f\n", q1_15_to_float(a2));
    int16_t su =
        lookup_table_interpolate(sinh_wave_table, u / N_Cheb, 14); // Q2.14
    // printf("su %f\n", q2_14_to_float(su));
    int16_t cu =
        lookup_table_interpolate(cosh_wave_table, u / N_Cheb, 14); // Q2.14

    // printf("su %f\n", q2_14_to_float(su));
    // printf("cu %f\n", q2_14_to_float(cu));

    for (int i = 0; i < m; ++i) {
        // But we don't need to multiply by 2Pi since our wave table is sampled
        // by x*2*pi
        int16_t z = ((int32_t)(2 * i + 1) * (1 << 13) / N_Cheb);
        // printf("i = %d z_su = %f\n", z_su, q1_15_to_float(z_su));
        // printf("i = %d z_cu = %f\n", i, q1_15_to_float(z_cu));
        int16_t b =
            (int32_t)lookup_table_interpolate(sine_wave_table, z, 15) * su >>
            14;
        //
        // b = ;
        // printf("i = %d b = %f\n", i, q1_15_to_float(b));
        int16_t c =
            (int32_t)lookup_table_interpolate(cos_wave_table, z, 15) * cu >> 14;

        // printf("i = %d c = %f\n", i, q1_15_to_float(c));
        c = ((int32_t)b * b + (int32_t)c * c) >>
            16; // this can overflow I think

        // printf("i = %d c_2 = %f\n", i, q2_14_to_float(c));

        int16_t a2_c = ((int32_t)a2 * c) >> 14;    // q1.15
        int16_t ab_2 = 2 * ((int32_t)a * b) >> 15; // q1.15

        // s = ((int32_t) a2 * c) >> 15  //q2.30 -> q1.15*q2.14 = q3.29
        //     + (((int32_t) a * b) >> 14 ) *2  //q2.30
        //   + (1 << 14); //q2.14
        int16_t s = (1 << 14);
        s += a2_c >> 1; // q2.30 -> q1.15*q2.14 = q3.29
        s += ab_2 >> 1; // q2.30

        // printf("i = %d s = %f\n", i, q2_14_to_float(s));
        A[i] = (((int32_t)a2 << 14) / s) >> 2;          // 4.0 q1.15
        d1[i] = 2 * (((1 << 15) - a2_c) << 13) / s;     // q2.14
        d2[i] = (-(a2_c - ab_2 + (1 << 15)) << 14) / s; // q1.15
        // printf("i = %d, A[i] = %lf, d1[i] = %lf, d2[i] = %lf\n", i,
        //        q1_15_to_float(A[i]), q2_14_to_float(d1[i]),
        //        q1_15_to_float(d2[i]));
    }
}

int16_t FilterCheb::che_low_pass(int16_t x) {
    // ep is q2.14
    // x is q1.15
    // d1 is q2.14
    // d2 is q1.15
    // A is q1.15
    // maybe w should be q2.14, I don't really know
    // maybe q4.12 to play it safe, we can try and see if it works
    for (int i = 0; i < m; ++i) {
        int16_t d1w1 = ((int32_t)d1[i] * w1[i]) >> 14; // q4.12
        int16_t d2w2 = ((int32_t)d2[i] * w2[i]) >> 15; // q4.12
        w0[i] = d1w1 + d2w2 + (x >> 3); // this does not feel right, if x is
                                        // small this will kill it
        x = ((int32_t)A[i] * (w0[i] + 2 * w1[i] + w2[i])) >> 12;
        w2[i] = w1[i];
        w1[i] = w0[i];
        // x = (x <<);
    }
    return x;
}


void FilterCheb::out(int16_t *samples, size_t size) {
    for (size_t i = 0; i < size; i++) {
        samples[i] = che_low_pass(samples[i]);
    }
}
