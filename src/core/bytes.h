#ifndef __BYTES_H__
#define __BYTES_H__

#include <stdint.h>

// Little-endian reads from a byte buffer, with no alignment requirements

static inline uint16_t Bytes_ReadU16(const uint8_t* p) {
	return (uint16_t)(p[0] | (p[1] << 8));
}

static inline uint32_t Bytes_ReadU32(const uint8_t* p) {
	return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24);
}

#endif
