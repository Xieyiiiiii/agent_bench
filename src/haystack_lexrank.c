/*
 * Benchmark:
 *   haystack_lexrank
 *
 * Reference archive:
 *   reference/haystack_lexrank/source_excerpt.md
 *   reference/haystack_lexrank/analysis.md
 *   reference/haystack_lexrank/analysis_zh.md
 *
 * Open-source references:
 *   1. Repo: crabcamp/lexrank
 *      File: README.rst
 *      Function / class / method: LexRank summary flow
 *      URL: https://github.com/crabcamp/lexrank/blob/dev/README.rst
 *      Referenced semantics: sentence similarity graph and centrality.
 *   2. Repo: miso-belica/sumy
 *      File: sumy/summarizers/lex_rank.py
 *      Function / class / method: LexRankSummarizer
 *      URL: https://github.com/miso-belica/sumy/blob/main/sumy/summarizers/lex_rank.py
 *      Referenced semantics: LexRank-style extractive selection.
 *   3. Repo: networkx/networkx
 *      File: networkx/algorithms/link_analysis/pagerank_alg.py
 *      Function / class / method: pagerank
 *      URL: https://github.com/networkx/networkx/blob/main/networkx/algorithms/link_analysis/pagerank_alg.py
 *      Referenced semantics: damping and incoming rank propagation background.
 *
 * C benchmark behavior:
 *   - Build a directed threshold graph from deterministic sentence_sim_q8 values.
 *   - Run fixed Q8 rank iterations with dangling rank redistribution.
 *   - Select summary sentences by rank, skipping redundant sentences.
 *
 * Benchmark-only extensions:
 *   - Fixed sentence-term matrix, fixed iterations and fixed-point rank.
 *
 * Simplifications:
 *   - Raw text tokenizer and cosine details are replaced by sentence_sim_q8().
 *   - Fixed iteration replaces convergence checking; rank is not renormalized.
 *
 * Not implemented:
 *   - Full LexRank API, NetworkX graph object, tokenizer, text rendering.
 *
 * This file reimplements a small deterministic C benchmark from the
 * referenced workload semantics. It does not copy framework structure and
 * does not depend on the reference project at build or run time.
 */

#include <stdint.h>
#include <stdio.h>
#include "bench_config.h"
#include "checksum.h"
#include "fixed_point.h"

#define SIM_THRESHOLD_Q8 128
#define REDUNDANT_THRESHOLD_Q8 128

typedef struct {
    uint16_t terms[MAX_SENTENCES][MAX_TERMS_SENT];
    uint16_t tf[MAX_SENTENCES][MAX_TERMS_SENT];
    uint8_t len[MAX_SENTENCES];
} SentenceCorpus;

typedef struct {
    uint8_t graph[MAX_SENTENCES][MAX_SENTENCES];
    uint8_t out_degree[MAX_SENTENCES];
    int32_t rank_old_q8[MAX_SENTENCES];
    int32_t rank_new_q8[MAX_SENTENCES];
    int32_t sim_q8[MAX_SENTENCES][MAX_SENTENCES];
} LexRankState;

typedef struct {
    int32_t selected[SUMMARY_K];
    uint32_t checksum;
} LexRankResult;

typedef struct {
    int32_t edges;
    int32_t redundant_skips;
    int32_t iterations;
} LexRankCounters;

static void init_corpus(SentenceCorpus *corpus)
{
    const uint16_t terms[MAX_SENTENCES][MAX_TERMS_SENT] = {
        {1u, 2u, 3u, 0u, 0u},
        {1u, 2u, 4u, 0u, 0u},
        {5u, 6u, 0u, 0u, 0u},
        {5u, 6u, 7u, 0u, 0u},
        {8u, 9u, 0u, 0u, 0u},
        {10u, 11u, 0u, 0u, 0u}
    };
    const uint8_t lens[MAX_SENTENCES] = {3u, 3u, 2u, 3u, 2u, 2u};
    int32_t i;
    int32_t j;

    for (i = 0; i < MAX_SENTENCES; ++i) {
        corpus->len[i] = lens[i];
        for (j = 0; j < MAX_TERMS_SENT; ++j) {
            corpus->terms[i][j] = terms[i][j];
            corpus->tf[i][j] = terms[i][j] == 0u ? 0u : 1u;
        }
    }
}

static void reset_state(LexRankState *state, LexRankResult *result,
                        LexRankCounters *counters)
{
    int32_t i;
    int32_t j;

    for (i = 0; i < MAX_SENTENCES; ++i) {
        state->out_degree[i] = 0u;
        state->rank_old_q8[i] = Q8_ONE / MAX_SENTENCES;
        state->rank_new_q8[i] = 0;
        for (j = 0; j < MAX_SENTENCES; ++j) {
            state->graph[i][j] = 0u;
            state->sim_q8[i][j] = 0;
        }
    }
    for (i = 0; i < SUMMARY_K; ++i) {
        result->selected[i] = -1;
    }
    result->checksum = 0;
    counters->edges = 0;
    counters->redundant_skips = 0;
    counters->iterations = 0;
}

static int term_exists(const SentenceCorpus *corpus, int32_t sent,
                       uint16_t term)
{
    int32_t i;

    for (i = 0; i < corpus->len[sent]; ++i) {
        if (corpus->terms[sent][i] == term) {
            return 1;
        }
    }
    return 0;
}

static int32_t sentence_sim_q8(const SentenceCorpus *corpus, int32_t left,
                               int32_t right)
{
    int32_t i;
    int32_t common = 0;
    int32_t denom = corpus->len[left] > corpus->len[right]
                        ? corpus->len[left]
                        : corpus->len[right];

    if (denom == 0) {
        return 0;
    }
    for (i = 0; i < corpus->len[left]; ++i) {
        if (term_exists(corpus, right, corpus->terms[left][i])) {
            common++;
        }
    }
    return (int32_t)(((int64_t)common * Q8_ONE) / denom);
}

static void compute_similarity_matrix(const SentenceCorpus *corpus,
                                      LexRankState *state)
{
    int32_t i;
    int32_t j;

    for (i = 0; i < MAX_SENTENCES; ++i) {
        for (j = 0; j < MAX_SENTENCES; ++j) {
            if (i == j) {
                state->sim_q8[i][j] = Q8_ONE;
            } else {
                state->sim_q8[i][j] = sentence_sim_q8(corpus, i, j);
            }
        }
    }
}

static void build_threshold_graph(LexRankState *state,
                                  LexRankCounters *counters)
{
    int32_t src;
    int32_t dst;

    for (src = 0; src < MAX_SENTENCES; ++src) {
        for (dst = 0; dst < MAX_SENTENCES; ++dst) {
            if (src != dst && state->sim_q8[src][dst] >= SIM_THRESHOLD_Q8) {
                state->graph[src][dst] = 1u;
                state->out_degree[src]++;
                counters->edges++;
            }
        }
    }
}

static void run_rank_iterations(LexRankState *state, LexRankCounters *counters)
{
    const int32_t damping_q8 = 218;
    const int32_t base_q8 = (Q8_ONE - damping_q8) / MAX_SENTENCES;
    int32_t iter;

    for (iter = 0; iter < RANK_ITERS; ++iter) {
        int32_t dst;
        int32_t src;
        int32_t dangling_sum_q8 = 0;
        int32_t dangling_share_q8;

        for (src = 0; src < MAX_SENTENCES; ++src) {
            if (state->out_degree[src] == 0u) {
                dangling_sum_q8 += state->rank_old_q8[src];
            }
        }
        dangling_share_q8 = dangling_sum_q8 / MAX_SENTENCES;

        for (dst = 0; dst < MAX_SENTENCES; ++dst) {
            int32_t incoming_q8 = dangling_share_q8;
            for (src = 0; src < MAX_SENTENCES; ++src) {
                if (state->graph[src][dst] && state->out_degree[src] > 0u) {
                    incoming_q8 += state->rank_old_q8[src] /
                                   (int32_t)state->out_degree[src];
                }
            }
            state->rank_new_q8[dst] = base_q8 + q8_mul(damping_q8, incoming_q8);
        }

        for (src = 0; src < MAX_SENTENCES; ++src) {
            state->rank_old_q8[src] = state->rank_new_q8[src];
        }
        counters->iterations++;
    }
}

static int sentence_is_redundant(const LexRankState *state,
                                 const LexRankResult *result, int32_t sent)
{
    int32_t i;

    for (i = 0; i < SUMMARY_K; ++i) {
        int32_t selected = result->selected[i];
        if (selected >= 0 &&
            state->sim_q8[sent][selected] >= REDUNDANT_THRESHOLD_Q8) {
            return 1;
        }
    }
    return 0;
}

static int candidate_better(const LexRankState *state, int32_t left,
                            int32_t right)
{
    if (right < 0) {
        return 1;
    }
    if (state->rank_old_q8[left] != state->rank_old_q8[right]) {
        return state->rank_old_q8[left] > state->rank_old_q8[right];
    }
    return left < right;
}

static void select_summary_sentences(const LexRankState *state,
                                     LexRankResult *result,
                                     LexRankCounters *counters)
{
    uint8_t used[MAX_SENTENCES] = {0u};
    int32_t out;

    for (out = 0; out < SUMMARY_K; ++out) {
        int32_t best = -1;
        int32_t sent;

        for (sent = 0; sent < MAX_SENTENCES; ++sent) {
            if (!used[sent] && candidate_better(state, sent, best)) {
                best = sent;
            }
        }
        if (best < 0) {
            return;
        }
        used[best] = 1u;
        if (sentence_is_redundant(state, result, best)) {
            counters->redundant_skips++;
            --out;
            continue;
        }
        result->selected[out] = best;
    }
}

static void run_kernel(const SentenceCorpus *corpus, LexRankState *state,
                       LexRankResult *result, LexRankCounters *counters)
{
    compute_similarity_matrix(corpus, state);
    build_threshold_graph(state, counters);
    run_rank_iterations(state, counters);
    select_summary_sentences(state, result, counters);
}

static uint32_t checksum_result(const LexRankState *state,
                                const LexRankResult *result,
                                const LexRankCounters *counters)
{
    uint32_t checksum = 2166136261u;
    int32_t i;

    for (i = 0; i < SUMMARY_K; ++i) {
        checksum = checksum_mix(checksum, result->selected[i]);
        if (result->selected[i] >= 0) {
            checksum = checksum_mix(checksum,
                                    state->rank_old_q8[result->selected[i]]);
        }
    }
    checksum = checksum_mix(checksum, counters->edges);
    checksum = checksum_mix(checksum, counters->redundant_skips);
    checksum = checksum_mix(checksum, counters->iterations);
    return checksum;
}

static void print_selected(const LexRankResult *result)
{
    int32_t i;

    printf("SELECTED_SENTENCES=");
    for (i = 0; i < SUMMARY_K; ++i) {
        printf("%s%d", i == 0 ? "" : ",", result->selected[i]);
    }
    printf("\n");
}

static void print_result(const LexRankResult *result,
                         const LexRankCounters *counters)
{
    printf("KERNEL=haystack_lexrank\n");
    printf("DANGLING_POLICY=redistribute_q8\n");
    print_selected(result);
    printf("EDGES=%d\n", counters->edges);
    printf("ITERATIONS=%d\n", counters->iterations);
    printf("REDUNDANT_SKIPS=%d\n", counters->redundant_skips);
    printf("CHECKSUM=%u\n", result->checksum);
}

int main(void)
{
    SentenceCorpus corpus;
    LexRankState state;
    LexRankResult result;
    LexRankCounters counters;

    init_corpus(&corpus);
    reset_state(&state, &result, &counters);
    run_kernel(&corpus, &state, &result, &counters);
    result.checksum = checksum_result(&state, &result, &counters);
    print_result(&result, &counters);
    return 0;
}
