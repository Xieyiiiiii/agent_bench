/*
 * CGRA kernel slice: hybrid_merge_ingest_core
 * Reference slice: reference/haystack_hybrid_merge/analysis_zh.md
 * Output layout:
 *   merged_doc[0..7]       = merged doc IDs, invalid = -1
 *   merged_dense_q8[0..7]  = dense scores, missing = 0
 *   merged_sparse_q8[0..7] = sparse scores, missing = 0
 *   merged_mask[0..7]      = bit0 dense present, bit1 sparse present
 *   out[0] = merged_count
 *   out[1] = duplicates
 *   out[2] = filtered
 *   out[3] = overflow
 */

#define CGRA_MERGE_MAX 8
#define CGRA_INVALID_ID (-1)

int hybrid_merge_ingest_core(int *dense_doc, int *dense_score_q8,
                             int *sparse_doc, int *sparse_score_q8,
                             int *doc_domain, int *doc_flags,
                             int *merged_doc, int *merged_dense_q8,
                             int *merged_sparse_q8, int *merged_mask,
                             int *out, int dense_k, int sparse_k)
{
    int merged_count = 0;
    int duplicates = 0;
    int filtered = 0;
    int overflow = 0;
    int i = 0;

    for (i = 0; i < CGRA_MERGE_MAX; i = i + 1) {
        merged_doc[i] = CGRA_INVALID_ID;
        merged_dense_q8[i] = 0;
        merged_sparse_q8[i] = 0;
        merged_mask[i] = 0;
    }

    for (i = 0; i < dense_k; i = i + 1) {
        int doc_id = dense_doc[i];
        int slot = -1;
        int pass = 1;
        int j = 0;

        if (doc_domain[doc_id] != 1 || (doc_flags[doc_id] & 1) == 0) {
            pass = 0;
            filtered = filtered + 1;
        }

        if (pass != 0) {
            for (j = 0; j < merged_count; j = j + 1) {
                if (merged_doc[j] == doc_id) {
                    slot = j;
                }
            }
            if (slot < 0) {
                if (merged_count >= CGRA_MERGE_MAX) {
                    overflow = overflow + 1;
                } else {
                    slot = merged_count;
                    merged_doc[slot] = doc_id;
                    merged_count = merged_count + 1;
                }
            }
            if (slot >= 0) {
                merged_dense_q8[slot] = dense_score_q8[i];
                merged_mask[slot] = merged_mask[slot] | 1;
            }
        }
    }

    for (i = 0; i < sparse_k; i = i + 1) {
        int doc_id = sparse_doc[i];
        int slot = -1;
        int pass = 1;
        int j = 0;

        if (doc_domain[doc_id] != 1 || (doc_flags[doc_id] & 1) == 0) {
            pass = 0;
            filtered = filtered + 1;
        }

        if (pass != 0) {
            for (j = 0; j < merged_count; j = j + 1) {
                if (merged_doc[j] == doc_id) {
                    slot = j;
                }
            }
            if (slot >= 0 && (merged_mask[slot] & 1) != 0) {
                duplicates = duplicates + 1;
            }
            if (slot < 0) {
                if (merged_count >= CGRA_MERGE_MAX) {
                    overflow = overflow + 1;
                } else {
                    slot = merged_count;
                    merged_doc[slot] = doc_id;
                    merged_count = merged_count + 1;
                }
            }
            if (slot >= 0) {
                merged_sparse_q8[slot] = sparse_score_q8[i];
                merged_mask[slot] = merged_mask[slot] | 2;
            }
        }
    }

    out[0] = merged_count;
    out[1] = duplicates;
    out[2] = filtered;
    out[3] = overflow;
    return 0;
}
