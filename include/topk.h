#ifndef TOPK_H
#define TOPK_H

#include <stdint.h>
#include "bench_config.h"

typedef struct {
    int32_t doc_id;
    int32_t score;
} TopKItem;

static inline void reset_topk(TopKItem topk[TOP_K])
{
    int32_t i;
    for (i = 0; i < TOP_K; ++i) {
        topk[i].doc_id = -1;
        topk[i].score = 0;
    }
}

static inline int topk_boundary_is_valid(const TopKItem topk[TOP_K])
{
    return topk[TOP_K - 1].doc_id >= 0;
}

static inline int topk_min_distance_better(int32_t doc_id, int32_t score,
                                           const TopKItem *item)
{
    if (item->doc_id < 0) {
        return 1;
    }
    if (score != item->score) {
        return score < item->score;
    }
    return doc_id < item->doc_id;
}

static inline void update_topk_min_distance(TopKItem topk[TOP_K],
                                            int32_t doc_id, int32_t score)
{
    int32_t pos;
    for (pos = 0; pos < TOP_K; ++pos) {
        if (topk_min_distance_better(doc_id, score, &topk[pos])) {
            int32_t shift;
            for (shift = TOP_K - 1; shift > pos; --shift) {
                topk[shift] = topk[shift - 1];
            }
            topk[pos].doc_id = doc_id;
            topk[pos].score = score;
            return;
        }
    }
}

static inline int topk_max_score_better(int32_t doc_id, int32_t score,
                                        const TopKItem *item)
{
    if (item->doc_id < 0) {
        return 1;
    }
    if (score != item->score) {
        return score > item->score;
    }
    return doc_id < item->doc_id;
}

static inline void update_topk_max_score(TopKItem topk[TOP_K],
                                         int32_t doc_id, int32_t score)
{
    int32_t pos;
    for (pos = 0; pos < TOP_K; ++pos) {
        if (topk_max_score_better(doc_id, score, &topk[pos])) {
            int32_t shift;
            for (shift = TOP_K - 1; shift > pos; --shift) {
                topk[shift] = topk[shift - 1];
            }
            topk[pos].doc_id = doc_id;
            topk[pos].score = score;
            return;
        }
    }
}

#endif
