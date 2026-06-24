#ifndef RNG_H
#define RNG_H

#include <stdint.h>

static inline uint32_t next_lcg(uint32_t *state)
{
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

#endif
