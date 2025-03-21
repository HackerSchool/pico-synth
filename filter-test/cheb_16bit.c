#include <array>
#include <cstddef>
#include <cstdint>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Compile: gcc -lm -o cheblpf cheblpf.c
//
//
const int WAVE_TABLE_LEN = 512;
const int16_t Q15_MAX = 32767;
const int16_t Q14_MAX = 16384;

// Convert float to Q2.14 format (2 integer bits, 14 fractional bits)
int16_t float_to_q2_14(float value) {
    return static_cast<int16_t>(round(value * (1 << 14)));
}

// Convert float to Q1.15 format (1 integer bit, 15 fractional bits)
int16_t float_to_q1_15(float value) {
    return static_cast<int16_t>(round(value * (1 << 15)));
}

// Convert float to Q8.8 format (8 integer bits, 8 fractional bits)
int16_t float_to_q8_8(float value) {
    return static_cast<int16_t>(round(value * (1 << 8)));
}

// Convert Q2.14 format (2 integer bits, 14 fractional bits) to float
float q2_14_to_float(int16_t value) {
    return static_cast<float>(value) / (1 << 14);
}

// Convert Q1.15 format (1 integer bit, 15 fractional bits) to float
float q1_15_to_float(int16_t value) {
    return static_cast<float>(value) / (1 << 15);
}

// Convert Q8.8 format (8 integer bits, 8 fractional bits) to float
float q8_8_to_float(int16_t value) {
    return static_cast<float>(value) / (1 << 8);
}

// This method allows to define the wavetable at compile time!
// Sine Wavetable
const std::array<int16_t, WAVE_TABLE_LEN> sine_wave_table{[]() {
    std::array<int16_t, WAVE_TABLE_LEN> table{};
    for (int i = 0; i < WAVE_TABLE_LEN; i++) {
        table[i] =
            static_cast<int16_t>(32767 * sin(i * 2 * M_PI / WAVE_TABLE_LEN));
    }
    return table;
}()};

const std::array<int16_t, WAVE_TABLE_LEN> cos_wave_table{[]() {
    std::array<int16_t, WAVE_TABLE_LEN> table{};
    for (int i = 0; i < WAVE_TABLE_LEN; i++) {
        table[i] =
            static_cast<int16_t>(32767 * cos(i * 2 * M_PI / WAVE_TABLE_LEN));
    }
    return table;
}()};

const std::array<int16_t, WAVE_TABLE_LEN> tan_wave_table{[]() {
    std::array<int16_t, WAVE_TABLE_LEN> table{};
    for (int i = 0; i < WAVE_TABLE_LEN; i++) {
        table[i] = static_cast<int16_t>(
            32767 * tan(i * 0.25 * M_PI /
                        WAVE_TABLE_LEN)); // in this range it goes from 0 to 1
    }
    return table;
}()};

// Lookup tables for sinh, cosh, and u approximations
const std::array<int16_t, WAVE_TABLE_LEN> sinh_wave_table{[]() {
    std::array<int16_t, WAVE_TABLE_LEN> table{};
    for (int i = 0; i < WAVE_TABLE_LEN; i++) {
        double x = (1.0 * i / WAVE_TABLE_LEN); // Range (0,1]
        table[i] = static_cast<int16_t>(Q14_MAX * sinh(x));
    }
    return table;
}()};

const std::array<int16_t, WAVE_TABLE_LEN> cosh_wave_table{[]() {
    std::array<int16_t, WAVE_TABLE_LEN> table{};
    for (int i = 0; i < WAVE_TABLE_LEN; i++) {
        double x = (1.0 * i / WAVE_TABLE_LEN);
        table[i] = static_cast<int16_t>(Q14_MAX * cosh(x));
    }
    return table;
}()};

const std::array<int16_t, WAVE_TABLE_LEN> u_wave_table{[]() {
    std::array<int16_t, WAVE_TABLE_LEN> table{};
    for (int i = 1; i < WAVE_TABLE_LEN; i++) { // Start at 1 to avoid log(0)
        float x = (1.0 * i / WAVE_TABLE_LEN);  // Range (0,1]
        table[i] = float_to_q2_14(log((1.0 + sqrt(1.0 + x * x)) / x));
    }
    table[0] = table[1]; // Avoid issues with index 0
    return table;
}()};

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

int main() {

    size_t i, n = 8;
    int16_t m = n / 2;
    int16_t ep = float_to_q2_14(0.5f); // in ]0.3, 1]
    float fs = 44100;
    float fc = 5000;
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
    int16_t su = lookup_table_interpolate(sinh_wave_table, u / n, 14); // Q2.14
    // printf("su %f\n", q2_14_to_float(su));
    int16_t cu = lookup_table_interpolate(cosh_wave_table, u / n, 14); // Q2.14

    // printf("su %f\n", q2_14_to_float(su));
    // printf("cu %f\n", q2_14_to_float(cu));
    int16_t b, c;
    int16_t A[m];
    int16_t d1[m];
    int16_t d2[m];
    int16_t w0[m];
    int16_t w1[m];
    int16_t w2[m];
    int16_t x;
    int16_t s;

    for (i = 0; i < m; ++i) {
        // But we don't need to multiply by 2Pi since our wave table is sampled
        // by x*2*pi
        int16_t z = ((int32_t)(2 * i + 1) * (1 << 13) / n);
        // printf("i = %d z_su = %f\n", z_su, q1_15_to_float(z_su));
        // printf("i = %d z_cu = %f\n", i, q1_15_to_float(z_cu));
        b = (int32_t)lookup_table_interpolate(sine_wave_table, z, 15) * su >>
            14;
        //
        // b = ;
        // printf("i = %d b = %f\n", i, q1_15_to_float(b));
        c = (int32_t)lookup_table_interpolate(cos_wave_table, z, 15) * cu >> 14;

        // printf("i = %d c = %f\n", i, q1_15_to_float(c));
        c = ((int32_t)b * b + (int32_t)c * c) >>
            16; // this can overflow I think

        // printf("i = %d c_2 = %f\n", i, q2_14_to_float(c));

        int16_t a2_c = ((int32_t)a2 * c) >> 14;    // q1.15
        int16_t ab_2 = 2 * ((int32_t)a * b) >> 15; // q1.15

        // s = ((int32_t) a2 * c) >> 15  //q2.30 -> q1.15*q2.14 = q3.29
        //     + (((int32_t) a * b) >> 14 ) *2  //q2.30
        //   + (1 << 14); //q2.14
        s = (1 << 14);
        s += a2_c >> 1; // q2.30 -> q1.15*q2.14 = q3.29
        s += ab_2 >> 1; // q2.30

        // printf("i = %d s = %f\n", i, q2_14_to_float(s));
        A[i] = (((int32_t)a2 << 14) / s) >> 2;          // 4.0 q1.15
        d1[i] = 2.0 * (((1 << 15) - a2_c) << 13) / s;   // q2.14
        d2[i] = (-(a2_c - ab_2 + (1 << 15)) << 14) / s; // q1.15
        printf("i = %d, A[i] = %lf, d1[i] = %lf, d2[i] = %lf\n", i,
               q1_15_to_float(A[i]), q2_14_to_float(d1[i]),
               q1_15_to_float(d2[i]));
    }

    // ep = (int32_t)2 / ep; // used to normalize
    ep = 2;
    while (scanf("%f", &x) != EOF) {
        int16_t x = float_to_q1_15(x);
        // ep is q2.14
        // x is q1.15
        // d1 is q2.14
        // d2 is q1.15
        // A is q1.15
        // maybe w should be q2.14, I don't really know
        // maybe q4.12 to play it safe, we can try and see if it works
        for (i = 0; i < m; ++i) {
            int16_t d1w1 = ((int32_t)d1[i] * w1[i]) >> 14; // q4.12
            int16_t d2w2 = ((int32_t)d2[i] * w2[i]) >> 15; // q4.12
            w0[i] = d1w1 + d2w2 + (x >> 3); // this does not feel right, if x is
                                            // small this will kill it
            x = ((int32_t)A[i] * (w0[i] + 2 * w1[i] + w2[i])) >> 12;
            w2[i] = w1[i];
            w1[i] = w0[i];
            x = (x << 1);
        }
        printf("%f ", q1_15_to_float(x));
    }

    return (0);
}
