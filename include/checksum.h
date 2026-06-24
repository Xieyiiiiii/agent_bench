#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <stdint.h>

static inline uint32_t checksum_mix(uint32_t checksum, int32_t value)
{
    uint32_t x = (uint32_t)value;
    checksum ^= x + 0x9e3779b9u + (checksum << 6) + (checksum >> 2);
    return checksum;
}

#endif
