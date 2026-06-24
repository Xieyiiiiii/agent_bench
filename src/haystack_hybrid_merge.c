/*
 * Benchmark:
 *   haystack_hybrid_merge
 *
 * Reference archive:
 *   reference/haystack_hybrid_merge/source_excerpt.md
 *   reference/haystack_hybrid_merge/analysis.md
 *   reference/haystack_hybrid_merge/analysis_zh.md
 *
 * Open-source references:
 *   1. Repo: deepset-ai/haystack
 *      File: haystack/components/joiners/document_joiner.py
 *      Function / class / method: DocumentJoiner.run()
 *      URL: https://github.com/deepset-ai/haystack/blob/main/haystack/components/joiners/document_joiner.py
 *      Referenced semantics: join, duplicate handling and weighted score merge.
 *   2. Repo: castorini/pyserini
 *      File: README.md
 *      Function / class / method: hybrid retrieval examples
 *      URL: https://github.com/castorini/pyserini/blob/master/README.md
 *      Referenced semantics: hybrid retrieval background only; RRF is not v0 behavior.
 *
 * C benchmark behavior:
 *   - Merge dense and sparse candidate lists.
 *   - Count cross-source duplicates.
 *   - Apply metadata filter and pure weighted Q8 score merge.
 *   - Collect Top-K by merged_score_q8 descending, then smaller doc_id.
 *
 * Benchmark-only extensions:
 *   - Deterministic candidate lists and metadata chosen to exercise duplicates/filter.
 *
 * Simplifications:
 *   - Haystack Document objects are represented by fixed candidate arrays.
 *
 * Not implemented:
 *   - Reciprocal rank fusion, duplicate bonus, pipeline execution, dynamic documents.
 *
 * This file reimplements a small deterministic C benchmark from the
 * referenced workload semantics. It does not copy framework structure and
 * does not depend on the reference project at build or run time.
 */

#include <stdint.h>
#include <stdio.h>
#include "bench_common.h"
#include "checksum.h"
#include "fixed_point.h"

#define SOURCE_DENSE 0
#define SOURCE_SPARSE 1

typedef struct {
    int32_t doc_id;
    int32_t score_q8;
} Candidate;

typedef struct {
    Candidate dense[DENSE_K];
    Candidate sparse[SPARSE_K];
    uint8_t doc_domain[NUM_DOCS];
    uint8_t doc_flags[NUM_DOCS];
} HybridInput;

typedef struct {
    int32_t doc_id;
    int32_t dense_score_q8;
    int32_t sparse_score_q8;
    int32_t merged_score_q8;
    uint8_t has_dense;
    uint8_t has_sparse;
} MergedCandidate;

typedef struct {
    MergedCandidate merged[MERGE_MAX];
    int32_t merged_count;
    TopKItem topk[TOP_K];
} HybridState;

typedef struct {
    int32_t duplicates;
    int32_t filtered;
    int32_t overflow;
} HybridCounters;

static void init_data(HybridInput *input)
{
    int32_t i;
    const Candidate dense[DENSE_K] = {
        {1, 230}, {2, 210}, {3, 200}, {4, 180}, {8, 160}, {12, 120}
    };
    const Candidate sparse[SPARSE_K] = {
        {3, 240}, {5, 215}, {6, 190}, {8, 170}, {9, 160}, {13, 110}
    };

    for (i = 0; i < DENSE_K; ++i) {
        input->dense[i] = dense[i];
    }
    for (i = 0; i < SPARSE_K; ++i) {
        input->sparse[i] = sparse[i];
    }
    for (i = 0; i < NUM_DOCS; ++i) {
        input->doc_domain[i] = 1u;
        input->doc_flags[i] = 1u;
    }
    input->doc_domain[12] = 0u;
    input->doc_flags[13] = 0u;
}

static void reset_state(HybridState *state, HybridCounters *counters)
{
    int32_t i;

    state->merged_count = 0;
    reset_topk(state->topk);
    for (i = 0; i < MERGE_MAX; ++i) {
        state->merged[i].doc_id = -1;
        state->merged[i].dense_score_q8 = 0;
        state->merged[i].sparse_score_q8 = 0;
        state->merged[i].merged_score_q8 = 0;
        state->merged[i].has_dense = 0u;
        state->merged[i].has_sparse = 0u;
    }
    counters->duplicates = 0;
    counters->filtered = 0;
    counters->overflow = 0;
}

static int passes_filter(const HybridInput *input, int32_t doc_id)
{
    return doc_id >= 0 && doc_id < NUM_DOCS &&
           input->doc_domain[doc_id] == 1u &&
           (input->doc_flags[doc_id] & 1u) != 0u;
}

static int32_t find_merged(const HybridState *state, int32_t doc_id)
{
    int32_t i;

    for (i = 0; i < state->merged_count; ++i) {
        if (state->merged[i].doc_id == doc_id) {
            return i;
        }
    }
    return -1;
}

static int32_t insert_or_find_merged(HybridState *state, int32_t doc_id,
                                     HybridCounters *counters)
{
    int32_t index = find_merged(state, doc_id);

    if (index >= 0) {
        return index;
    }
    if (state->merged_count >= MERGE_MAX) {
        counters->overflow++;
        return -1;
    }
    index = state->merged_count++;
    state->merged[index].doc_id = doc_id;
    return index;
}

static void merge_candidate_list(const HybridInput *input, HybridState *state,
                                 HybridCounters *counters,
                                 const Candidate *list, int32_t count,
                                 int32_t source)
{
    int32_t i;

    for (i = 0; i < count; ++i) {
        int32_t slot;
        int32_t doc_id = list[i].doc_id;

        if (!passes_filter(input, doc_id)) {
            counters->filtered++;
            continue;
        }
        slot = insert_or_find_merged(state, doc_id, counters);
        if (slot < 0) {
            continue;
        }
        if (source == SOURCE_DENSE) {
            state->merged[slot].dense_score_q8 = list[i].score_q8;
            state->merged[slot].has_dense = 1u;
        } else {
            if (state->merged[slot].has_dense) {
                counters->duplicates++;
            }
            state->merged[slot].sparse_score_q8 = list[i].score_q8;
            state->merged[slot].has_sparse = 1u;
        }
    }
}

static int32_t weighted_merge_score_q8(const MergedCandidate *candidate)
{
    const int32_t dense_weight_q8 = 154;
    const int32_t sparse_weight_q8 = Q8_ONE - dense_weight_q8;
    int32_t dense_component_q8 = 0;
    int32_t sparse_component_q8 = 0;

    if (candidate->has_dense) {
        dense_component_q8 =
            q8_mul(dense_weight_q8, candidate->dense_score_q8);
    }
    if (candidate->has_sparse) {
        sparse_component_q8 =
            q8_mul(sparse_weight_q8, candidate->sparse_score_q8);
    }
    return dense_component_q8 + sparse_component_q8;
}

static void score_merged_candidates_q8(HybridState *state)
{
    int32_t i;

    for (i = 0; i < state->merged_count; ++i) {
        state->merged[i].merged_score_q8 =
            weighted_merge_score_q8(&state->merged[i]);
    }
}

static void collect_merged_topk(HybridState *state)
{
    int32_t i;

    reset_topk(state->topk);
    for (i = 0; i < state->merged_count; ++i) {
        update_topk_max_score(state->topk, state->merged[i].doc_id,
                              state->merged[i].merged_score_q8);
    }
}

static void run_kernel(const HybridInput *input, HybridState *state,
                       HybridCounters *counters)
{
    merge_candidate_list(input, state, counters, input->dense, DENSE_K,
                         SOURCE_DENSE);
    merge_candidate_list(input, state, counters, input->sparse, SPARSE_K,
                         SOURCE_SPARSE);
    score_merged_candidates_q8(state);
    collect_merged_topk(state);
}

static uint32_t checksum_result(const HybridState *state,
                                const HybridCounters *counters)
{
    uint32_t checksum = 2166136261u;
    int32_t i;

    for (i = 0; i < TOP_K; ++i) {
        checksum = checksum_mix(checksum, state->topk[i].doc_id);
        checksum = checksum_mix(checksum, state->topk[i].score);
    }
    checksum = checksum_mix(checksum, counters->duplicates);
    checksum = checksum_mix(checksum, counters->filtered);
    checksum = checksum_mix(checksum, counters->overflow);
    return checksum;
}

static void print_result(const HybridState *state,
                         const HybridCounters *counters, uint32_t checksum)
{
    printf("KERNEL=haystack_hybrid_merge\n");
    printf("MERGE_MODE=weighted_q8\n");
    print_topk_ids(state->topk);
    print_topk_scores(state->topk);
    printf("DUPLICATES=%d\n", counters->duplicates);
    printf("FILTERED=%d\n", counters->filtered);
    printf("OVERFLOW=%d\n", counters->overflow);
    printf("CHECKSUM=%u\n", checksum);
}

int main(void)
{
    HybridInput input;
    HybridState state;
    HybridCounters counters;
    uint32_t checksum;

    init_data(&input);
    reset_state(&state, &counters);
    run_kernel(&input, &state, &counters);
    checksum = checksum_result(&state, &counters);
    print_result(&state, &counters, checksum);
    return 0;
}
