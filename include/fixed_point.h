#ifndef FIXED_POINT_H
#define FIXED_POINT_H

#include <stdint.h>

#define Q8_ONE 256

static inline int32_t q8_mul(int32_t a_q8, int32_t b_q8)
{
    return (int32_t)(((int64_t)a_q8 * (int64_t)b_q8) / Q8_ONE);
}

static inline int32_t q8_div(int32_t numer_q8, int32_t denom_q8)
{
    if (denom_q8 == 0) {
        return 0;
    }
    return (int32_t)(((int64_t)numer_q8 * Q8_ONE) / denom_q8);
}

#endif
