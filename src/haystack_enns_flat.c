/*
 * Benchmark:
 *   haystack_enns_flat
 *
 * Reference archive:
 *   reference/haystack_enns_flat/source_excerpt.md
 *   reference/haystack_enns_flat/analysis.md
 *   reference/haystack_enns_flat/analysis_zh.md
 *
 * Open-source references:
 *   1. Repo: deepset-ai/haystack
 *      File: haystack/components/retrievers/in_memory/embedding_retriever.py
 *      Function / class / method: InMemoryEmbeddingRetriever.run()
 *      URL: https://github.com/deepset-ai/haystack/blob/main/haystack/components/retrievers/in_memory/embedding_retriever.py
 *      Referenced semantics: query embedding, top_k, retriever flow.
 *   2. Repo: facebookresearch/faiss
 *      File: faiss/IndexFlat.cpp
 *      Function / class / method: IndexFlat::search(), IndexFlatL2
 *      URL: https://github.com/facebookresearch/faiss/blob/main/faiss/IndexFlat.cpp
 *      Referenced semantics: exhaustive squared L2 scan.
 *
 * C benchmark behavior:
 *   - Scan every synthetic document vector.
 *   - Compute squared L2 distance.
 *   - Keep deterministic insertion Top-K by smaller distance, then doc_id.
 *
 * Benchmark-only extensions:
 *   - Deterministic int16 synthetic vectors and checksum output.
 *
 * Simplifications:
 *   - Haystack Document objects are fixed arrays.
 *   - FAISS heap utilities are replaced by insertion Top-K.
 *
 * Not implemented:
 *   - Haystack pipeline, document store, embedding model, FAISS index object.
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
    int16_t db[NUM_DOCS][DIM];
    int16_t query[DIM];
} EnnsFlatInput;

typedef struct {
    TopKItem topk[TOP_K];
    uint32_t checksum;
} EnnsFlatResult;

typedef struct {
    int32_t docs_scanned;
} EnnsFlatCounters;

static void init_data(EnnsFlatInput *input)
{
    int32_t doc;
    int32_t dim;

    for (dim = 0; dim < DIM; ++dim) {
        input->query[dim] = (int16_t)(dim * 3 - 7);
    }
    for (doc = 0; doc < NUM_DOCS; ++doc) {
        for (dim = 0; dim < DIM; ++dim) {
            input->db[doc][dim] =
                (int16_t)(input->query[dim] + ((doc * 5 + dim * 2) % 17) - 8);
        }
    }
}

static void reset_result(EnnsFlatResult *result, EnnsFlatCounters *counters)
{
    reset_topk(result->topk);
    result->checksum = 0;
    counters->docs_scanned = 0;
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

static void run_kernel(const EnnsFlatInput *input, EnnsFlatResult *result,
                       EnnsFlatCounters *counters)
{
    int32_t doc;

    for (doc = 0; doc < NUM_DOCS; ++doc) {
        int32_t dist = l2_distance(input->query, input->db[doc]);
        update_topk_min_distance(result->topk, doc, dist);
        counters->docs_scanned++;
    }
}

static uint32_t checksum_result(const EnnsFlatResult *result,
                                const EnnsFlatCounters *counters)
{
    uint32_t checksum = 2166136261u;
    int32_t i;

    for (i = 0; i < TOP_K; ++i) {
        checksum = checksum_mix(checksum, result->topk[i].doc_id);
        checksum = checksum_mix(checksum, result->topk[i].score);
    }
    checksum = checksum_mix(checksum, counters->docs_scanned);
    return checksum;
}

static void print_result(const EnnsFlatResult *result,
                         const EnnsFlatCounters *counters)
{
    printf("KERNEL=haystack_enns_flat\n");
    print_topk_ids(result->topk);
    print_topk_scores(result->topk);
    printf("DOCS_SCANNED=%d\n", counters->docs_scanned);
    printf("CHECKSUM=%u\n", result->checksum);
}

int main(void)
{
    EnnsFlatInput input;
    EnnsFlatResult result;
    EnnsFlatCounters counters;

    init_data(&input);
    reset_result(&result, &counters);
    run_kernel(&input, &result, &counters);
    result.checksum = checksum_result(&result, &counters);
    print_result(&result, &counters);
    return 0;
}
