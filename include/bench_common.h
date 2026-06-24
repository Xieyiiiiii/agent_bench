#ifndef BENCH_COMMON_H
#define BENCH_COMMON_H

#include <stdint.h>
#include <stdio.h>
#include "bench_config.h"
#include "topk.h"

static inline void print_topk_ids(const TopKItem topk[TOP_K])
{
    int32_t i;
    printf("TOPK_IDS=");
    for (i = 0; i < TOP_K; ++i) {
        printf("%s%d", i == 0 ? "" : ",", topk[i].doc_id);
    }
    printf("\n");
}

static inline void print_topk_scores(const TopKItem topk[TOP_K])
{
    int32_t i;
    printf("TOPK_SCORES=");
    for (i = 0; i < TOP_K; ++i) {
        printf("%s%d", i == 0 ? "" : ",", topk[i].score);
    }
    printf("\n");
}

#endif
