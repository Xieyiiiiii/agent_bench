/*
 * CGRA kernel slice: hybrid_merge_core
 * Reference slice: reference/haystack_hybrid_merge/analysis_zh.md
 * Output layout:
 *   out[0..3] = topk doc IDs
 *   out[4..7] = topk merged scores
 *   out[8]    = duplicates
 *   out[9]    = filtered
 *   out[10]   = overflow
 */

#define CGRA_TOP_K 4
#define CGRA_MERGE_MAX 12
#define CGRA_INVALID_ID (-1)
#define CGRA_NEG_SCORE (-2147483000)
#define CGRA_Q8_ONE 256
#define CGRA_DENSE_WEIGHT_Q8 154
#define CGRA_SPARSE_WEIGHT_Q8 102

int hybrid_merge_core(int *dense_doc, int *dense_score_q8,
                      int *sparse_doc, int *sparse_score_q8,
                      int *doc_domain, int *doc_flags, int *out,
                      int dense_k, int sparse_k)
{
    int merged_doc[CGRA_MERGE_MAX];
    int merged_dense_q8[CGRA_MERGE_MAX];
    int merged_sparse_q8[CGRA_MERGE_MAX];
    int merged_has_dense[CGRA_MERGE_MAX];
    int merged_has_sparse[CGRA_MERGE_MAX];
    int merged_count = 0;
    int duplicates = 0;
    int filtered = 0;
    int overflow = 0;
    int i = 0;
    int top_id0 = CGRA_INVALID_ID;
    int top_id1 = CGRA_INVALID_ID;
    int top_id2 = CGRA_INVALID_ID;
    int top_id3 = CGRA_INVALID_ID;
    int top_sc0 = CGRA_NEG_SCORE;
    int top_sc1 = CGRA_NEG_SCORE;
    int top_sc2 = CGRA_NEG_SCORE;
    int top_sc3 = CGRA_NEG_SCORE;

    for (i = 0; i < CGRA_MERGE_MAX; i = i + 1) {
        merged_doc[i] = CGRA_INVALID_ID;
        merged_dense_q8[i] = 0;
        merged_sparse_q8[i] = 0;
        merged_has_dense[i] = 0;
        merged_has_sparse[i] = 0;
    }

    for (i = 0; i < dense_k; i = i + 1) {
        int doc_id = dense_doc[i];
        int slot = -1;
        int j = 0;
        if (doc_domain[doc_id] != 1 || (doc_flags[doc_id] & 1) == 0) {
            filtered = filtered + 1;
            continue;
        }
        for (j = 0; j < merged_count; j = j + 1) {
            if (merged_doc[j] == doc_id) {
                slot = j;
            }
        }
        if (slot < 0) {
            if (merged_count >= CGRA_MERGE_MAX) {
                overflow = overflow + 1;
                continue;
            }
            slot = merged_count;
            merged_doc[slot] = doc_id;
            merged_count = merged_count + 1;
        }
        merged_dense_q8[slot] = dense_score_q8[i];
        merged_has_dense[slot] = 1;
    }

    for (i = 0; i < sparse_k; i = i + 1) {
        int doc_id = sparse_doc[i];
        int slot = -1;
        int j = 0;
        if (doc_domain[doc_id] != 1 || (doc_flags[doc_id] & 1) == 0) {
            filtered = filtered + 1;
            continue;
        }
        for (j = 0; j < merged_count; j = j + 1) {
            if (merged_doc[j] == doc_id) {
                slot = j;
            }
        }
        if (slot >= 0 && merged_has_dense[slot] != 0) {
            duplicates = duplicates + 1;
        }
        if (slot < 0) {
            if (merged_count >= CGRA_MERGE_MAX) {
                overflow = overflow + 1;
                continue;
            }
            slot = merged_count;
            merged_doc[slot] = doc_id;
            merged_count = merged_count + 1;
        }
        merged_sparse_q8[slot] = sparse_score_q8[i];
        merged_has_sparse[slot] = 1;
    }

    for (i = 0; i < merged_count; i = i + 1) {
        int score = 0;
        int doc_id = merged_doc[i];
        if (merged_has_dense[i] != 0) {
            score = score + (CGRA_DENSE_WEIGHT_Q8 * merged_dense_q8[i]) / CGRA_Q8_ONE;
        }
        if (merged_has_sparse[i] != 0) {
            score = score + (CGRA_SPARSE_WEIGHT_Q8 * merged_sparse_q8[i]) / CGRA_Q8_ONE;
        }

        if ((score > top_sc0) || ((score == top_sc0) && (doc_id < top_id0 || top_id0 < 0))) {
            top_sc3 = top_sc2; top_id3 = top_id2;
            top_sc2 = top_sc1; top_id2 = top_id1;
            top_sc1 = top_sc0; top_id1 = top_id0;
            top_sc0 = score; top_id0 = doc_id;
        } else if ((score > top_sc1) || ((score == top_sc1) && (doc_id < top_id1 || top_id1 < 0))) {
            top_sc3 = top_sc2; top_id3 = top_id2;
            top_sc2 = top_sc1; top_id2 = top_id1;
            top_sc1 = score; top_id1 = doc_id;
        } else if ((score > top_sc2) || ((score == top_sc2) && (doc_id < top_id2 || top_id2 < 0))) {
            top_sc3 = top_sc2; top_id3 = top_id2;
            top_sc2 = score; top_id2 = doc_id;
        } else if ((score > top_sc3) || ((score == top_sc3) && (doc_id < top_id3 || top_id3 < 0))) {
            top_sc3 = score; top_id3 = doc_id;
        }
    }

    out[0] = top_id0; out[1] = top_id1; out[2] = top_id2; out[3] = top_id3;
    out[4] = top_sc0; out[5] = top_sc1; out[6] = top_sc2; out[7] = top_sc3;
    out[8] = duplicates;
    out[9] = filtered;
    out[10] = overflow;
    return 0;
}
