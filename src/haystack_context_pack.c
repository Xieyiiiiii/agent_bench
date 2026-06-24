/*
 * Benchmark:
 *   haystack_context_pack
 *
 * Reference archive:
 *   reference/haystack_context_pack/source_excerpt.md
 *   reference/haystack_context_pack/analysis.md
 *   reference/haystack_context_pack/analysis_zh.md
 *
 * Open-source references:
 *   1. Repo: deepset-ai/haystack
 *      File: haystack/components/builders/prompt_builder.py
 *      Function / class / method: PromptBuilder.run()
 *      URL: https://github.com/deepset-ai/haystack/blob/main/haystack/components/builders/prompt_builder.py
 *      Referenced semantics: consume documents before template rendering.
 *
 * C benchmark behavior:
 *   - Sort retrieved context candidates by score descending.
 *   - Deduplicate by source_id and chunk_id.
 *   - Pack documents into a fixed token budget with deterministic truncation.
 *
 * Benchmark-only extensions:
 *   - Score sort, dedup, budget packing and truncation are benchmark-defined.
 *
 * Simplifications:
 *   - Haystack Document objects are fixed candidate arrays.
 *   - Text payloads are represented by token_len only.
 *
 * Not implemented:
 *   - Jinja rendering, prompt string construction, tokenizer, LLM calls.
 *
 * This file reimplements a small deterministic C benchmark from the
 * referenced workload semantics. It does not copy framework structure and
 * does not depend on the reference project at build or run time.
 */

#include <stdint.h>
#include <stdio.h>
#include "bench_config.h"
#include "checksum.h"

typedef struct {
    int32_t doc_id;
    int32_t source_id;
    int32_t chunk_id;
    uint16_t token_len;
    int32_t score;
} ContextCandidate;

typedef struct {
    int32_t doc_id;
    int32_t source_id;
    int32_t chunk_id;
    uint16_t used_tokens;
    uint8_t truncated;
} PackedDoc;

typedef struct {
    ContextCandidate candidates[CONTEXT_K];
} ContextPackInput;

typedef struct {
    PackedDoc packed[CONTEXT_K];
    int32_t packed_count;
    int32_t used_tokens;
    uint32_t checksum;
} ContextPackResult;

typedef struct {
    int32_t truncated;
    int32_t skipped_duplicate;
    int32_t skipped_budget;
} ContextPackCounters;

static void init_data(ContextPackInput *input)
{
    const ContextCandidate candidates[CONTEXT_K] = {
        {4, 1, 0, 18u, 220},
        {1, 0, 0, 20u, 210},
        {2, 0, 1, 16u, 205},
        {3, 0, 1, 12u, 190},
        {5, 2, 0, 30u, 180},
        {6, 3, 0, 9u, 170},
        {7, 4, 0, 7u, 160},
        {8, 5, 0, 6u, 150}
    };
    int32_t i;

    for (i = 0; i < CONTEXT_K; ++i) {
        input->candidates[i] = candidates[i];
    }
}

static void reset_result(ContextPackResult *result,
                         ContextPackCounters *counters)
{
    int32_t i;

    result->packed_count = 0;
    result->used_tokens = 0;
    result->checksum = 0;
    for (i = 0; i < CONTEXT_K; ++i) {
        result->packed[i].doc_id = -1;
        result->packed[i].source_id = -1;
        result->packed[i].chunk_id = -1;
        result->packed[i].used_tokens = 0u;
        result->packed[i].truncated = 0u;
    }
    counters->truncated = 0;
    counters->skipped_duplicate = 0;
    counters->skipped_budget = 0;
}

static int candidate_better(const ContextCandidate *left,
                            const ContextCandidate *right)
{
    if (left->score != right->score) {
        return left->score > right->score;
    }
    return left->doc_id < right->doc_id;
}

static void sort_candidates_by_score(ContextPackInput *input)
{
    int32_t i;

    for (i = 1; i < CONTEXT_K; ++i) {
        ContextCandidate item = input->candidates[i];
        int32_t j = i - 1;
        while (j >= 0 && candidate_better(&item, &input->candidates[j])) {
            input->candidates[j + 1] = input->candidates[j];
            --j;
        }
        input->candidates[j + 1] = item;
    }
}

static int is_duplicate_source_chunk(const ContextPackResult *result,
                                     const ContextCandidate *candidate)
{
    int32_t i;

    for (i = 0; i < result->packed_count; ++i) {
        if (result->packed[i].source_id == candidate->source_id &&
            result->packed[i].chunk_id == candidate->chunk_id) {
            return 1;
        }
    }
    return 0;
}

static int32_t remaining_budget(const ContextPackResult *result)
{
    return TOKEN_BUDGET - result->used_tokens;
}

static void append_packed_doc(ContextPackResult *result,
                              const ContextCandidate *candidate,
                              uint16_t used_tokens, uint8_t truncated)
{
    PackedDoc *slot = &result->packed[result->packed_count++];

    slot->doc_id = candidate->doc_id;
    slot->source_id = candidate->source_id;
    slot->chunk_id = candidate->chunk_id;
    slot->used_tokens = used_tokens;
    slot->truncated = truncated;
    result->used_tokens += used_tokens;
}

static void pack_candidate_with_budget(ContextPackResult *result,
                                       ContextPackCounters *counters,
                                       const ContextCandidate *candidate)
{
    int32_t remaining = remaining_budget(result);

    if (remaining <= 0) {
        counters->skipped_budget++;
        return;
    }
    if (candidate->token_len <= (uint16_t)remaining) {
        append_packed_doc(result, candidate, candidate->token_len, 0u);
        return;
    }
    if (remaining >= MIN_TRUNC_TOKENS) {
        append_packed_doc(result, candidate, (uint16_t)remaining, 1u);
        counters->truncated++;
        return;
    }
    counters->skipped_budget++;
}

static void run_kernel(ContextPackInput *input, ContextPackResult *result,
                       ContextPackCounters *counters)
{
    int32_t i;

    sort_candidates_by_score(input);
    for (i = 0; i < CONTEXT_K; ++i) {
        if (is_duplicate_source_chunk(result, &input->candidates[i])) {
            counters->skipped_duplicate++;
            continue;
        }
        pack_candidate_with_budget(result, counters, &input->candidates[i]);
    }
}

static uint32_t checksum_result(const ContextPackResult *result,
                                const ContextPackCounters *counters)
{
    uint32_t checksum = 2166136261u;
    int32_t i;

    for (i = 0; i < result->packed_count; ++i) {
        checksum = checksum_mix(checksum, result->packed[i].doc_id);
        checksum = checksum_mix(checksum, result->packed[i].used_tokens);
        checksum = checksum_mix(checksum, result->packed[i].truncated);
    }
    checksum = checksum_mix(checksum, result->used_tokens);
    checksum = checksum_mix(checksum, counters->truncated);
    checksum = checksum_mix(checksum, counters->skipped_duplicate);
    checksum = checksum_mix(checksum, counters->skipped_budget);
    return checksum;
}

static void print_packed_ids(const ContextPackResult *result)
{
    int32_t i;

    printf("PACKED_DOC_IDS=");
    for (i = 0; i < result->packed_count; ++i) {
        printf("%s%d", i == 0 ? "" : ",", result->packed[i].doc_id);
    }
    printf("\n");
}

static void print_result(const ContextPackResult *result,
                         const ContextPackCounters *counters)
{
    printf("KERNEL=haystack_context_pack\n");
    print_packed_ids(result);
    printf("USED_TOKENS=%d\n", result->used_tokens);
    printf("TRUNCATED=%d\n", counters->truncated);
    printf("SKIPPED_DUPLICATE=%d\n", counters->skipped_duplicate);
    printf("SKIPPED_BUDGET=%d\n", counters->skipped_budget);
    printf("CHECKSUM=%u\n", result->checksum);
}

int main(void)
{
    ContextPackInput input;
    ContextPackResult result;
    ContextPackCounters counters;

    init_data(&input);
    reset_result(&result, &counters);
    run_kernel(&input, &result, &counters);
    result.checksum = checksum_result(&result, &counters);
    print_result(&result, &counters);
    return 0;
}
