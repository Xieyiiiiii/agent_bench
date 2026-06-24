/*
 * Benchmark:
 *   haystack_bm25
 *
 * Reference archive:
 *   reference/haystack_bm25/source_excerpt.md
 *   reference/haystack_bm25/analysis.md
 *   reference/haystack_bm25/analysis_zh.md
 *
 * Open-source references:
 *   1. Repo: deepset-ai/haystack
 *      File: haystack/components/retrievers/in_memory/bm25_retriever.py
 *      Function / class / method: InMemoryBM25Retriever.run()
 *      URL: https://github.com/deepset-ai/haystack/blob/main/haystack/components/retrievers/in_memory/bm25_retriever.py
 *      Referenced semantics: query/filter/top_k retriever flow.
 *   2. Repo: dorianbrown/rank_bm25
 *      File: rank_bm25.py
 *      Function / class / method: BM25Okapi.get_scores()
 *      URL: https://github.com/dorianbrown/rank_bm25/blob/master/rank_bm25.py
 *      Referenced semantics: BM25Okapi scoring target.
 *   3. Repo: xhluca/bm25s
 *      File: bm25s/scoring.py
 *      Function / class / method: sparse scoring routines
 *      URL: https://github.com/xhluca/bm25s/blob/main/bm25s/scoring.py
 *      Referenced semantics: posting-list layout background only.
 *
 * C benchmark behavior:
 *   - Traverse deterministic query term posting lists.
 *   - Apply fixed metadata filter before scoring each posting.
 *   - Accumulate BM25Okapi-style Q8 term contributions.
 *   - Collect Top-K from active documents by larger score, then smaller doc_id.
 *
 * Benchmark-only extensions:
 *   - Deterministic synthetic postings, doc lengths, metadata and idf_q8 input.
 *
 * Simplifications:
 *   - Python float scoring is replaced by fixed-point Q8 integer scoring.
 *   - Tokenization, corpus preprocessing and sparse matrix internals are omitted.
 *
 * Not implemented:
 *   - BM25Okapi._calc_idf(), negative-IDF epsilon floor, tokenizer, BM25L.
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

typedef struct {
    int32_t doc_id;
    uint16_t tf;
} Posting;

typedef struct {
    int32_t start;
    int32_t len;
} PostingList;

typedef struct {
    uint16_t doc_len[NUM_DOCS];
    uint8_t doc_domain[NUM_DOCS];
    int32_t idf_q8[VOCAB_SIZE];
    PostingList lists[VOCAB_SIZE];
    Posting postings[MAX_POSTINGS];
} Bm25Index;

typedef struct {
    uint16_t terms[NUM_QUERY_TERMS];
} Bm25Query;

typedef struct {
    int32_t score_q8[NUM_DOCS];
    uint8_t active[NUM_DOCS];
    TopKItem topk[TOP_K];
} Bm25State;

typedef struct {
    int32_t active_docs;
    int32_t filtered_out;
    int32_t empty_terms;
} Bm25Counters;

static void add_postings(Bm25Index *index, uint16_t term, int32_t start,
                         const Posting *items, int32_t count)
{
    int32_t i;

    index->lists[term].start = start;
    index->lists[term].len = count;
    for (i = 0; i < count; ++i) {
        index->postings[start + i] = items[i];
    }
}

static void init_index(Bm25Index *index)
{
    int32_t i;
    const Posting term1[] = {{0, 3u}, {1, 2u}, {2, 4u}, {3, 1u}, {4, 2u}};
    const Posting term3[] = {{2, 1u}, {5, 3u}, {6, 2u}, {7, 4u}, {8, 1u}};
    const Posting term5[] = {{1, 1u}, {4, 3u}, {7, 2u}, {9, 5u}, {10, 1u}};

    for (i = 0; i < NUM_DOCS; ++i) {
        index->doc_len[i] = (uint16_t)(64 + (i * 7) % 41);
        index->doc_domain[i] = (uint8_t)(i % 3 == 0 ? 0 : 1);
    }
    for (i = 0; i < VOCAB_SIZE; ++i) {
        index->idf_q8[i] = 180 + i * 13;
        index->lists[i].start = 0;
        index->lists[i].len = 0;
    }
    for (i = 0; i < MAX_POSTINGS; ++i) {
        index->postings[i].doc_id = -1;
        index->postings[i].tf = 0u;
    }

    add_postings(index, 1u, 0, term1, (int32_t)(sizeof(term1) / sizeof(term1[0])));
    add_postings(index, 3u, 5, term3, (int32_t)(sizeof(term3) / sizeof(term3[0])));
    add_postings(index, 5u, 10, term5, (int32_t)(sizeof(term5) / sizeof(term5[0])));
}

static void init_query(Bm25Query *query)
{
    query->terms[0] = 1u;
    query->terms[1] = 3u;
    query->terms[2] = 5u;
    query->terms[3] = 11u;
}

static void reset_state(Bm25State *state, Bm25Counters *counters)
{
    int32_t i;

    for (i = 0; i < NUM_DOCS; ++i) {
        state->score_q8[i] = 0;
        state->active[i] = 0u;
    }
    reset_topk(state->topk);
    counters->active_docs = 0;
    counters->filtered_out = 0;
    counters->empty_terms = 0;
}

static int passes_filter(const Bm25Index *index, int32_t doc_id)
{
    return doc_id >= 0 && doc_id < NUM_DOCS && index->doc_domain[doc_id] == 1u;
}

static int32_t bm25_term_score_q8(uint16_t tf, uint16_t doc_len,
                                  int32_t idf_q8)
{
    const int32_t k1_q8 = 384;
    const int32_t b_q8 = 192;
    const int32_t avg_doc_len = 80;
    int32_t norm_q8;
    int32_t denom_q8;
    int32_t tf_num_q8;
    int32_t tf_weight_q8;

    if (avg_doc_len == 0) {
        return 0;
    }
    norm_q8 = Q8_ONE - b_q8 + (int32_t)(((int64_t)b_q8 * doc_len) / avg_doc_len);
    denom_q8 = (int32_t)tf * Q8_ONE + q8_mul(k1_q8, norm_q8);
    if (denom_q8 == 0) {
        return 0;
    }
    tf_num_q8 = (int32_t)tf * (k1_q8 + Q8_ONE);
    tf_weight_q8 = q8_div(tf_num_q8, denom_q8);
    return q8_mul(idf_q8, tf_weight_q8);
}

static void score_posting_list_q8(const Bm25Index *index, uint16_t term,
                                  Bm25State *state, Bm25Counters *counters)
{
    PostingList list = index->lists[term];
    int32_t i;

    for (i = 0; i < list.len; ++i) {
        const Posting *posting = &index->postings[list.start + i];
        int32_t doc_id = posting->doc_id;

        if (!passes_filter(index, doc_id)) {
            counters->filtered_out++;
            continue;
        }
        state->score_q8[doc_id] +=
            bm25_term_score_q8(posting->tf, index->doc_len[doc_id],
                               index->idf_q8[term]);
        state->active[doc_id] = 1u;
    }
}

static void collect_active_docs_into_topk(Bm25State *state,
                                          Bm25Counters *counters)
{
    int32_t doc;

    for (doc = 0; doc < NUM_DOCS; ++doc) {
        if (state->active[doc]) {
            counters->active_docs++;
            update_topk_max_score(state->topk, doc, state->score_q8[doc]);
        }
    }
}

static void run_kernel(const Bm25Index *index, const Bm25Query *query,
                       Bm25State *state, Bm25Counters *counters)
{
    int32_t i;

    for (i = 0; i < NUM_QUERY_TERMS; ++i) {
        uint16_t term = query->terms[i];
        if (index->lists[term].len == 0) {
            counters->empty_terms++;
            continue;
        }
        score_posting_list_q8(index, term, state, counters);
    }
    collect_active_docs_into_topk(state, counters);
}

static uint32_t checksum_result(const Bm25State *state,
                                const Bm25Counters *counters)
{
    uint32_t checksum = 2166136261u;
    int32_t i;

    for (i = 0; i < TOP_K; ++i) {
        checksum = checksum_mix(checksum, state->topk[i].doc_id);
        checksum = checksum_mix(checksum, state->topk[i].score);
    }
    checksum = checksum_mix(checksum, counters->active_docs);
    checksum = checksum_mix(checksum, counters->filtered_out);
    checksum = checksum_mix(checksum, counters->empty_terms);
    return checksum;
}

static void print_result(const Bm25State *state, const Bm25Counters *counters,
                         uint32_t checksum)
{
    printf("KERNEL=haystack_bm25\n");
    print_topk_ids(state->topk);
    print_topk_scores(state->topk);
    printf("ACTIVE_DOCS=%d\n", counters->active_docs);
    printf("FILTERED_OUT=%d\n", counters->filtered_out);
    printf("EMPTY_TERMS=%d\n", counters->empty_terms);
    printf("CHECKSUM=%u\n", checksum);
}

int main(void)
{
    Bm25Index index;
    Bm25Query query;
    Bm25State state;
    Bm25Counters counters;
    uint32_t checksum;

    init_index(&index);
    init_query(&query);
    reset_state(&state, &counters);
    run_kernel(&index, &query, &state, &counters);
    checksum = checksum_result(&state, &counters);
    print_result(&state, &counters, checksum);
    return 0;
}
