#ifndef __BYTES_H__
#define __BYTES_H__

#include <stdint.h>
#include <math.h>

// Little-endian reads from a byte buffer, with no alignment requirements

static inline uint16_t Bytes_ReadU16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t Bytes_ReadU32(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

// IEEE 754 binary16
static inline float Bytes_ReadF16(const uint8_t* p) {
    uint16_t half = Bytes_ReadU16(p);
    int exponent = (half >> 10) & 0x1F;
    int mantissa = half & 0x3FF;
    float value;
    if (exponent == 0)
        value = ldexpf((float)mantissa, -24);
    else if (exponent == 31)
        value = mantissa != 0 ? NAN : INFINITY;
    else
        value = ldexpf((float)(mantissa | 0x400), exponent - 25);
    return (half & 0x8000) ? -value : value;
}

#endif
