#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

cat > "$TMP_DIR/context_pack_harness.c" <<'C_EOF'
#include <assert.h>

int context_pack_core(int *doc_id, int *source_id, int *chunk_id,
                      int *token_len, int *score, int *out, int count);

int main(void)
{
    int out[8] = {0};
    int i = 0;

    {
        int doc_id[4] = {10, 11, 12, 13};
        int source_id[4] = {1, 2, 3, 4};
        int chunk_id[4] = {1, 1, 1, 1};
        int token_len[4] = {6, 7, 8, 9};
        int score[4] = {100, 90, 80, 70};
        context_pack_core(doc_id, source_id, chunk_id, token_len, score, out, 4);
        assert(out[0] == 10);
        assert(out[4] == 30);
        assert(out[5] == 0);
        assert(out[6] == 0);
        assert(out[7] == 0);
    }

    for (i = 0; i < 8; i = i + 1) {
        out[i] = 0;
    }
    {
        int doc_id[4] = {20, 21, 22, 23};
        int source_id[4] = {1, 1, 2, 3};
        int chunk_id[4] = {1, 1, 1, 1};
        int token_len[4] = {10, 10, 10, 10};
        int score[4] = {100, 90, 80, 70};
        context_pack_core(doc_id, source_id, chunk_id, token_len, score, out, 4);
        assert(out[6] == 1);
        assert(out[4] == 30);
    }

    for (i = 0; i < 8; i = i + 1) {
        out[i] = 0;
    }
    {
        int doc_id[4] = {30, 31, 32, 33};
        int source_id[4] = {1, 2, 3, 4};
        int chunk_id[4] = {1, 1, 1, 1};
        int token_len[4] = {30, 30, 4, 4};
        int score[4] = {100, 90, 80, 70};
        context_pack_core(doc_id, source_id, chunk_id, token_len, score, out, 4);
        assert(out[4] == 48);
        assert(out[5] == 1);
        assert(out[7] >= 1);
    }

    for (i = 0; i < 4; i = i + 1) {
        assert(out[i] >= -1);
    }
    assert(out[4] <= 48);
    return 0;
}
C_EOF

cat > "$TMP_DIR/hybrid_merge_harness.c" <<'C_EOF'
#include <assert.h>

int hybrid_merge_ingest_core(int *dense_doc, int *dense_score_q8,
                             int *sparse_doc, int *sparse_score_q8,
                             int *doc_domain, int *doc_flags,
                             int *merged_doc, int *merged_dense_q8,
                             int *merged_sparse_q8, int *merged_mask,
                             int *out, int dense_k, int sparse_k);

int hybrid_merge_score_topk_core(int *merged_doc, int *merged_dense_q8,
                                 int *merged_sparse_q8, int *merged_mask,
                                 int *out, int merged_count);

int main(void)
{
    int dense_doc[5] = {0, 1, 2, 3, 4};
    int dense_score_q8[5] = {100, 200, 300, 400, 500};
    int sparse_doc[6] = {1, 5, 6, 7, 8, 9};
    int sparse_score_q8[6] = {1000, 600, 700, 800, 900, 1000};
    int doc_domain[10] = {1, 1, 1, 1, 0, 1, 1, 1, 1, 1};
    int doc_flags[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
    int merged_doc[8] = {0};
    int merged_dense_q8[8] = {0};
    int merged_sparse_q8[8] = {0};
    int merged_mask[8] = {0};
    int ingest_out[4] = {0};
    int score_out[8] = {0};

    hybrid_merge_ingest_core(dense_doc, dense_score_q8,
                             sparse_doc, sparse_score_q8,
                             doc_domain, doc_flags,
                             merged_doc, merged_dense_q8,
                             merged_sparse_q8, merged_mask,
                             ingest_out, 5, 6);

    assert(ingest_out[0] == 8);
    assert(ingest_out[1] == 1);
    assert(ingest_out[2] == 1);
    assert(ingest_out[3] == 1);
    assert(merged_doc[1] == 1);
    assert((merged_mask[1] & 3) == 3);

    hybrid_merge_score_topk_core(merged_doc, merged_dense_q8,
                                 merged_sparse_q8, merged_mask,
                                 score_out, ingest_out[0]);

    assert(score_out[0] == 1);
    assert(score_out[1] == 8);
    assert(score_out[2] == 7);
    assert(score_out[3] == 6);
    assert(merged_doc[1] == 1);
    assert((merged_mask[1] & 3) == 3);
    return 0;
}
C_EOF

gcc -std=c99 -Wall -Wextra -Werror -O0 -g -fsanitize=address \
  "$ROOT_DIR/cgra_kernels/context_pack_core.c" \
  "$TMP_DIR/context_pack_harness.c" \
  -o "$TMP_DIR/context_pack_harness"

ASAN_OPTIONS=detect_leaks=0 "$TMP_DIR/context_pack_harness"

gcc -std=c99 -Wall -Wextra -Werror -O0 -g -fsanitize=address \
  "$ROOT_DIR/cgra_kernels/hybrid_merge_ingest_core.c" \
  "$ROOT_DIR/cgra_kernels/hybrid_merge_score_topk_core.c" \
  "$TMP_DIR/hybrid_merge_harness.c" \
  -o "$TMP_DIR/hybrid_merge_harness"

ASAN_OPTIONS=detect_leaks=0 "$TMP_DIR/hybrid_merge_harness"

echo "check_cgra_behavior: ok"
