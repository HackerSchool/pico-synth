// fixed_point.h
#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <stdint.h>
#include <pico/types.h>

// Q16.16 fixed-point type and operations
typedef int32_t q16_16_t;

// Q8.24 fixed-point type and operations
typedef int32_t q8_24_t;

// Constants
#define Q16_FRAC_BITS 16
#define Q16_ONE (1 << Q16_FRAC_BITS)
#define Q16_HALF (Q16_ONE >> 1)

#define Q24_FRAC_BITS 24
#define Q24_ONE (1 << Q24_FRAC_BITS)
#define Q24_HALF (Q24_ONE >> 1)

// Q16.16 operations
inline q16_16_t q16_from_int(int32_t x) {
    return x << Q16_FRAC_BITS;
}

inline int32_t q16_to_int(q16_16_t x) {
    return x >> Q16_FRAC_BITS;
}

// Float conversion functions
inline q16_16_t q16_from_float(float x) {
    return (q16_16_t)(x * (float)Q16_ONE);
}

inline float q16_to_float(q16_16_t x) {
    return (float)x / (float)Q16_ONE;
}

inline q16_16_t q16_add(q16_16_t a, q16_16_t b) {
    return a + b;
}

inline q16_16_t q16_sub(q16_16_t a, q16_16_t b) {
    return a - b;
}

inline q16_16_t q16_mul(q16_16_t a, q16_16_t b) {
    // Use int64_t to avoid overflow during multiplication
    return (q16_16_t)(((int64_t)a * b) >> Q16_FRAC_BITS);
}

inline q16_16_t q16_div(q16_16_t a, q16_16_t b) {
    // Use int64_t to avoid overflow
    return (q16_16_t)(((int64_t)a << Q16_FRAC_BITS) / b);
}

// Q8.24 operations
inline q8_24_t q24_from_int(int32_t x) {
    return x << Q24_FRAC_BITS;
}

inline int32_t q24_to_int(q8_24_t x) {
    return x >> Q24_FRAC_BITS;
}

// Float conversion functions for Q8.24
inline q8_24_t q24_from_float(float x) {
    return (q8_24_t)(x * (float)Q24_ONE);
}

inline float q24_to_float(q8_24_t x) {
    return (float)x / (float)Q24_ONE;
}

inline q8_24_t q24_add(q8_24_t a, q8_24_t b) {
    return a + b;
}

inline q8_24_t q24_sub(q8_24_t a, q8_24_t b) {
    return a - b;
}

inline q8_24_t q24_mul(q8_24_t a, q8_24_t b) {
    return (q8_24_t)(((int64_t)a * b) >> Q24_FRAC_BITS);
}

inline q8_24_t q24_div(q8_24_t a, q8_24_t b) {
    return (q8_24_t)(((int64_t)a << Q24_FRAC_BITS) / b);
}

// Conversion between formats
inline q8_24_t q16_to_q24(q16_16_t x) {
    return x << (Q24_FRAC_BITS - Q16_FRAC_BITS);
}

inline q16_16_t q24_to_q16(q8_24_t x) {
    return x >> (Q24_FRAC_BITS - Q16_FRAC_BITS);
}


// Convert float to Q2.14 format (2 integer bits, 14 fractional bits)
inline int16_t float_to_q2_14(float value) {
    return static_cast<int16_t>(value * (1 << 14));
}

// Convert float to Q1.15 format (1 integer bit, 15 fractional bits)
inline int16_t float_to_q1_15(float value) {
    return static_cast<int16_t>(value * (1 << 15));
}

// Convert float to Q8.8 format (8 integer bits, 8 fractional bits)
inline int16_t float_to_q8_8(float value) {
    return static_cast<int16_t>(value * (1 << 8));
}

// Convert Q2.14 format (2 integer bits, 14 fractional bits) to float
inline float q2_14_to_float(int16_t value) {
    return static_cast<float>(value) / (1 << 14);
}

// Convert Q1.15 format (1 integer bit, 15 fractional bits) to float
inline float q1_15_to_float(int16_t value) {
    return static_cast<float>(value) / (1 << 15);
}

// Convert Q8.8 format (8 integer bits, 8 fractional bits) to float
inline float q8_8_to_float(int16_t value) {
    return static_cast<float>(value) / (1 << 8);
}

#endif // FIXED_POINT_H
