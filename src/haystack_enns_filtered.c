/*
 * Benchmark:
 *   haystack_enns_filtered
 *
 * Reference archive:
 *   reference/haystack_enns_filtered/source_excerpt.md
 *   reference/haystack_enns_filtered/analysis.md
 *   reference/haystack_enns_filtered/analysis_zh.md
 *
 * Open-source references:
 *   1. Repo: deepset-ai/haystack
 *      File: haystack/components/retrievers/in_memory/embedding_retriever.py
 *      Function / class / method: InMemoryEmbeddingRetriever.run()
 *      URL: https://github.com/deepset-ai/haystack/blob/main/haystack/components/retrievers/in_memory/embedding_retriever.py
 *      Referenced semantics: filters, query embedding and top_k retrieval flow.
 *   2. Repo: facebookresearch/faiss
 *      File: faiss/IndexFlat.cpp
 *      Function / class / method: IndexFlat::search(), IndexFlatL2
 *      URL: https://github.com/facebookresearch/faiss/blob/main/faiss/IndexFlat.cpp
 *      Referenced semantics: exhaustive squared L2 scan.
 *
 * C benchmark behavior:
 *   - Apply a fixed metadata predicate before distance computation.
 *   - Compute squared L2 for accepted documents.
 *   - Use early abandon only after Top-K boundary is valid.
 *
 * Benchmark-only extensions:
 *   - Early abandon cutoff for branch and partial accumulation stress.
 *   - Deterministic metadata designed to exercise filter/full/abandon paths.
 *
 * Simplifications:
 *   - Haystack filters are a fixed predicate, not a filter AST.
 *   - FAISS heap/index internals are replaced by insertion Top-K.
 *
 * Not implemented:
 *   - Haystack document store, filter parser, embedding model, FAISS index object.
 *
 * This file reimplements a small deterministic C benchmark from the
 * referenced workload semantics. It does not copy framework structure and
 * does not depend on the reference project at build or run time.
 */

#include <stdint.h>
#include <stdio.h>
#include "bench_common.h"
#include "checksum.h"

typedef struct {
    uint16_t year;
    uint8_t domain;
    uint8_t flags;
} DocMeta;

typedef struct {
    int16_t db[NUM_DOCS][DIM];
    int16_t query[DIM];
    DocMeta meta[NUM_DOCS];
} EnnsFilteredInput;

typedef struct {
    TopKItem topk[TOP_K];
    uint32_t checksum;
} EnnsFilteredResult;

typedef struct {
    int32_t filtered_out;
    int32_t distance_full;
    int32_t distance_abandoned;
    int32_t early_abandon_with_invalid_boundary;
} EnnsFilteredCounters;

static void init_data(EnnsFilteredInput *input)
{
    int32_t doc;
    int32_t dim;

    for (dim = 0; dim < DIM; ++dim) {
        input->query[dim] = (int16_t)(dim * 4);
    }

    for (doc = 0; doc < NUM_DOCS; ++doc) {
        input->meta[doc].year = (uint16_t)(2018 + (doc % 6));
        input->meta[doc].domain = 0u;
        input->meta[doc].flags = 0u;
        for (dim = 0; dim < DIM; ++dim) {
            input->db[doc][dim] = (int16_t)(input->query[dim] + 80 + doc);
        }
    }

    for (doc = 0; doc < 8; ++doc) {
        input->meta[doc].year = 2024u;
        input->meta[doc].domain = 1u;
        input->meta[doc].flags = 1u;
        for (dim = 0; dim < DIM; ++dim) {
            int16_t offset = doc < TOP_K ? (int16_t)(doc + dim % 2)
                                         : (int16_t)(90 + doc + dim);
            input->db[doc][dim] = (int16_t)(input->query[dim] + offset);
        }
    }
}

static void reset_result(EnnsFilteredResult *result,
                         EnnsFilteredCounters *counters)
{
    reset_topk(result->topk);
    result->checksum = 0;
    counters->filtered_out = 0;
    counters->distance_full = 0;
    counters->distance_abandoned = 0;
    counters->early_abandon_with_invalid_boundary = 0;
}

/* Fixed benchmark predicate: recent domain-1 documents with flag bit 0 set. */
static int passes_filter(const DocMeta *meta)
{
    return meta->year >= 2021u && meta->domain == 1u && (meta->flags & 1u) != 0u;
}

static int32_t l2_distance(const int16_t query[DIM], const int16_t doc[DIM])
{
    int32_t dim;
    int32_t dist = 0;

    for (dim = 0; dim < DIM; ++dim) {
        int32_t diff = (int32_t)query[dim] - (int32_t)doc[dim];
        dist += diff * diff;
    }
    return dist;
}

static int32_t l2_distance_until_cutoff(const int16_t query[DIM],
                                        const int16_t doc[DIM],
                                        int32_t cutoff, int *complete)
{
    int32_t dim;
    int32_t dist = 0;

    *complete = 1;
    for (dim = 0; dim < DIM; ++dim) {
        int32_t diff = (int32_t)query[dim] - (int32_t)doc[dim];
        dist += diff * diff;
        if (dist > cutoff) {
            *complete = 0;
            return dist;
        }
    }
    return dist;
}

static void run_kernel(const EnnsFilteredInput *input,
                       EnnsFilteredResult *result,
                       EnnsFilteredCounters *counters)
{
    int32_t doc;

    for (doc = 0; doc < NUM_DOCS; ++doc) {
        int complete = 1;
        int32_t dist;

        if (!passes_filter(&input->meta[doc])) {
            counters->filtered_out++;
            continue;
        }

        if (topk_boundary_is_valid(result->topk)) {
            dist = l2_distance_until_cutoff(input->query, input->db[doc],
                                            result->topk[TOP_K - 1].score,
                                            &complete);
        } else {
            dist = l2_distance(input->query, input->db[doc]);
        }

        if (complete) {
            counters->distance_full++;
            update_topk_min_distance(result->topk, doc, dist);
        } else {
            counters->distance_abandoned++;
        }
    }
}

static uint32_t checksum_result(const EnnsFilteredResult *result,
                                const EnnsFilteredCounters *counters)
{
    uint32_t checksum = 2166136261u;
    int32_t i;

    for (i = 0; i < TOP_K; ++i) {
        checksum = checksum_mix(checksum, result->topk[i].doc_id);
        checksum = checksum_mix(checksum, result->topk[i].score);
    }
    checksum = checksum_mix(checksum, counters->filtered_out);
    checksum = checksum_mix(checksum, counters->distance_full);
    checksum = checksum_mix(checksum, counters->distance_abandoned);
    checksum = checksum_mix(checksum, counters->early_abandon_with_invalid_boundary);
    return checksum;
}

static void print_result(const EnnsFilteredResult *result,
                         const EnnsFilteredCounters *counters)
{
    printf("KERNEL=haystack_enns_filtered\n");
    print_topk_ids(result->topk);
    print_topk_scores(result->topk);
    printf("FILTERED_OUT=%d\n", counters->filtered_out);
    printf("DISTANCE_FULL=%d\n", counters->distance_full);
    printf("DISTANCE_ABANDONED=%d\n", counters->distance_abandoned);
    printf("EARLY_ABANDON_WITH_INVALID_BOUNDARY=%d\n",
           counters->early_abandon_with_invalid_boundary);
    printf("CHECKSUM=%u\n", result->checksum);
}

int main(void)
{
    EnnsFilteredInput input;
    EnnsFilteredResult result;
    EnnsFilteredCounters counters;

    init_data(&input);
    reset_result(&result, &counters);
    run_kernel(&input, &result, &counters);
    result.checksum = checksum_result(&result, &counters);
    print_result(&result, &counters);
    return 0;
}
