#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

kernels=(
  haystack_enns_flat
  haystack_enns_filtered
  haystack_bm25
  haystack_hybrid_merge
  haystack_context_pack
  haystack_lexrank
)

require_file() {
  local path="$1"
  if [[ ! -f "$path" ]]; then
    echo "missing file: $path" >&2
    exit 1
  fi
}

require_pattern() {
  local pattern="$1"
  local path="$2"
  if ! grep -Eq "$pattern" "$path"; then
    echo "missing pattern '$pattern' in $path" >&2
    exit 1
  fi
}

require_value_gt_zero() {
  local key="$1"
  local path="$2"
  local value
  value="$(awk -F= -v key="$key" '$1 == key { print $2 }' "$path")"
  if [[ -z "$value" || "$value" -le 0 ]]; then
    echo "$key must be > 0 in $path, got '${value:-missing}'" >&2
    exit 1
  fi
}

for kernel in "${kernels[@]}"; do
  src="$ROOT_DIR/src/$kernel.c"
  out="$ROOT_DIR/build/$kernel.out"
  ref_dir="$ROOT_DIR/reference/$kernel"

  require_file "$src"
  require_file "$ref_dir/source_excerpt.md"
  require_file "$ref_dir/analysis.md"
  require_file "$ref_dir/analysis_zh.md"
  require_file "$out"

  require_pattern 'Reference archive:' "$src"
  require_pattern 'Benchmark-only extensions:' "$src"
  require_pattern 'Not implemented:' "$src"
  require_pattern 'CPU 瓶颈分析' "$ref_dir/analysis_zh.md"
  require_pattern '面向过程实现形态' "$ref_dir/analysis_zh.md"
  require_pattern '行为匹配检查' "$ref_dir/analysis_zh.md"
  require_pattern '^CHECKSUM=[0-9]+' "$out"
done

require_pattern '^TOPK_IDS=[0-9]' "$ROOT_DIR/build/haystack_enns_flat.out"
require_value_gt_zero DOCS_SCANNED "$ROOT_DIR/build/haystack_enns_flat.out"

require_pattern '^TOPK_IDS=[0-9]' "$ROOT_DIR/build/haystack_enns_filtered.out"
require_value_gt_zero FILTERED_OUT "$ROOT_DIR/build/haystack_enns_filtered.out"
require_value_gt_zero DISTANCE_FULL "$ROOT_DIR/build/haystack_enns_filtered.out"
require_value_gt_zero DISTANCE_ABANDONED "$ROOT_DIR/build/haystack_enns_filtered.out"
require_pattern '^EARLY_ABANDON_WITH_INVALID_BOUNDARY=0$' "$ROOT_DIR/build/haystack_enns_filtered.out"

require_pattern '^TOPK_IDS=[0-9]' "$ROOT_DIR/build/haystack_bm25.out"
require_value_gt_zero ACTIVE_DOCS "$ROOT_DIR/build/haystack_bm25.out"
require_value_gt_zero FILTERED_OUT "$ROOT_DIR/build/haystack_bm25.out"
require_value_gt_zero EMPTY_TERMS "$ROOT_DIR/build/haystack_bm25.out"
require_pattern 'idf_q8' "$ROOT_DIR/src/haystack_bm25.c"
if grep -Eq 'calculate_idf|calc_idf_q8|compute_idf|idf_calculator' "$ROOT_DIR/src/haystack_bm25.c"; then
  echo "BM25 must not calculate IDF in v0" >&2
  exit 1
fi

require_pattern '^TOPK_IDS=[0-9]' "$ROOT_DIR/build/haystack_hybrid_merge.out"
require_value_gt_zero DUPLICATES "$ROOT_DIR/build/haystack_hybrid_merge.out"
require_value_gt_zero FILTERED "$ROOT_DIR/build/haystack_hybrid_merge.out"
require_pattern '^MERGE_MODE=weighted_q8$' "$ROOT_DIR/build/haystack_hybrid_merge.out"
require_pattern 'merged_score_q8' "$ROOT_DIR/src/haystack_hybrid_merge.c"

require_pattern '^PACKED_DOC_IDS=[0-9]' "$ROOT_DIR/build/haystack_context_pack.out"
require_value_gt_zero USED_TOKENS "$ROOT_DIR/build/haystack_context_pack.out"
if ! awk -F= '$1 == "TRUNCATED" || $1 == "SKIPPED_DUPLICATE" || $1 == "SKIPPED_BUDGET" { if ($2 > 0) ok = 1 } END { exit ok ? 0 : 1 }' "$ROOT_DIR/build/haystack_context_pack.out"; then
  echo "context pack must exercise truncation or skip branch" >&2
  exit 1
fi

require_pattern '^SELECTED_SENTENCES=[0-9]' "$ROOT_DIR/build/haystack_lexrank.out"
require_value_gt_zero EDGES "$ROOT_DIR/build/haystack_lexrank.out"
require_value_gt_zero ITERATIONS "$ROOT_DIR/build/haystack_lexrank.out"
require_pattern '^DANGLING_POLICY=redistribute_q8$' "$ROOT_DIR/build/haystack_lexrank.out"
require_pattern 'out_degree.*== 0|out_degree.*= 0' "$ROOT_DIR/src/haystack_lexrank.c"
require_pattern 'rank_old_q8' "$ROOT_DIR/src/haystack_lexrank.c"

echo "check_outputs: ok"
