/*
 * CGRA kernel slice: hybrid_merge_score_topk_core
 * Reference slice: reference/haystack_hybrid_merge/analysis_zh.md
 * Output layout:
 *   out[0..3] = top doc IDs
 *   out[4..7] = top merged scores
 */

#define CGRA_TOP_K 4
#define CGRA_MERGE_MAX 8
#define CGRA_INVALID_ID (-1)
#define CGRA_NEG_SCORE (-2147483000)
#define CGRA_Q8_ONE 256
#define CGRA_DENSE_WEIGHT_Q8 154
#define CGRA_SPARSE_WEIGHT_Q8 102

int hybrid_merge_score_topk_core(int *merged_doc, int *merged_dense_q8,
                                 int *merged_sparse_q8, int *merged_mask,
                                 int *out, int merged_count)
{
    int top_id0 = CGRA_INVALID_ID;
    int top_id1 = CGRA_INVALID_ID;
    int top_id2 = CGRA_INVALID_ID;
    int top_id3 = CGRA_INVALID_ID;
    int top_sc0 = CGRA_NEG_SCORE;
    int top_sc1 = CGRA_NEG_SCORE;
    int top_sc2 = CGRA_NEG_SCORE;
    int top_sc3 = CGRA_NEG_SCORE;
    int limit = merged_count;
    int i = 0;

    if (limit > CGRA_MERGE_MAX) {
        limit = CGRA_MERGE_MAX;
    }
    if (limit < 0) {
        limit = 0;
    }

    for (i = 0; i < limit; i = i + 1) {
        int score = 0;
        int doc_id = merged_doc[i];

        if ((merged_mask[i] & 1) != 0) {
            score = score + (CGRA_DENSE_WEIGHT_Q8 * merged_dense_q8[i]) / CGRA_Q8_ONE;
        }
        if ((merged_mask[i] & 2) != 0) {
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
    return 0;
}
