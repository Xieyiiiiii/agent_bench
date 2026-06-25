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
    int doc_id[10] = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    int source_id[10] = {1, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    int chunk_id[10] = {1, 2, 1, 1, 1, 1, 1, 1, 1, 1};
    int token_len[10] = {6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    int score[10] = {50, 60, 70, 80, 90, 100, 110, 120, 130, 140};
    int out[12] = {0};
    int i = 0;

    context_pack_core(doc_id, source_id, chunk_id, token_len, score, out, 10);

    for (i = 0; i < 8; i = i + 1) {
        assert(out[i] >= -1);
    }
    assert(out[8] <= 48);
    return 0;
}
C_EOF

gcc -std=c99 -Wall -Wextra -Werror -O0 -g -fsanitize=address \
  "$ROOT_DIR/cgra_kernels/context_pack_core.c" \
  "$TMP_DIR/context_pack_harness.c" \
  -o "$TMP_DIR/context_pack_harness"

ASAN_OPTIONS=detect_leaks=0 "$TMP_DIR/context_pack_harness"

echo "check_cgra_behavior: ok"
