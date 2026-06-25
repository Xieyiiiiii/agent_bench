/*
 * CGRA kernel slice: context_pack_core
 * Reference slice: reference/haystack_context_pack/analysis_zh.md
 * Output layout:
 *   out[0..7]  = packed doc IDs, invalid = -1
 *   out[8]     = used_tokens
 *   out[9]     = truncated
 *   out[10]    = skipped_duplicate
 *   out[11]    = skipped_budget
 */

#define CGRA_CONTEXT_K 8
#define CGRA_TOKEN_BUDGET 48
#define CGRA_MIN_TRUNC_TOKENS 8
#define CGRA_INVALID_ID (-1)

int context_pack_core(int *doc_id, int *source_id, int *chunk_id,
                      int *token_len, int *score, int *out, int count)
{
    int used[CGRA_CONTEXT_K];
    int packed_doc[CGRA_CONTEXT_K];
    int packed_source[CGRA_CONTEXT_K];
    int packed_chunk[CGRA_CONTEXT_K];
    int packed_count = 0;
    int used_tokens = 0;
    int truncated = 0;
    int skipped_duplicate = 0;
    int skipped_budget = 0;
    int limit = count;
    int i = 0;

    if (limit > CGRA_CONTEXT_K) {
        limit = CGRA_CONTEXT_K;
    }
    if (limit < 0) {
        limit = 0;
    }

    for (i = 0; i < CGRA_CONTEXT_K; i = i + 1) {
        used[i] = 0;
        packed_doc[i] = CGRA_INVALID_ID;
        packed_source[i] = CGRA_INVALID_ID;
        packed_chunk[i] = CGRA_INVALID_ID;
    }

    for (i = 0; i < limit; i = i + 1) {
        int best = -1;
        int j = 0;
        int duplicate = 0;
        int remaining = 0;

        for (j = 0; j < limit; j = j + 1) {
            if (used[j] == 0) {
                if (best < 0) {
                    best = j;
                } else if (score[j] > score[best]) {
                    best = j;
                } else if (score[j] == score[best] && doc_id[j] < doc_id[best]) {
                    best = j;
                }
            }
        }
        if (best < 0) {
            i = limit;
            continue;
        }
        used[best] = 1;

        for (j = 0; j < packed_count; j = j + 1) {
            if (packed_source[j] == source_id[best] && packed_chunk[j] == chunk_id[best]) {
                duplicate = 1;
            }
        }
        if (duplicate != 0) {
            skipped_duplicate = skipped_duplicate + 1;
            continue;
        }

        remaining = CGRA_TOKEN_BUDGET - used_tokens;
        if (remaining <= 0) {
            skipped_budget = skipped_budget + 1;
            continue;
        }
        if (token_len[best] <= remaining) {
            if (packed_count < CGRA_CONTEXT_K) {
                packed_doc[packed_count] = doc_id[best];
                packed_source[packed_count] = source_id[best];
                packed_chunk[packed_count] = chunk_id[best];
                packed_count = packed_count + 1;
                used_tokens = used_tokens + token_len[best];
            }
        } else if (remaining >= CGRA_MIN_TRUNC_TOKENS) {
            if (packed_count < CGRA_CONTEXT_K) {
                packed_doc[packed_count] = doc_id[best];
                packed_source[packed_count] = source_id[best];
                packed_chunk[packed_count] = chunk_id[best];
                packed_count = packed_count + 1;
                used_tokens = used_tokens + remaining;
                truncated = truncated + 1;
            }
        } else {
            skipped_budget = skipped_budget + 1;
        }
    }

    for (i = 0; i < CGRA_CONTEXT_K; i = i + 1) {
        out[i] = packed_doc[i];
    }
    out[8] = used_tokens;
    out[9] = truncated;
    out[10] = skipped_duplicate;
    out[11] = skipped_budget;
    return 0;
}
