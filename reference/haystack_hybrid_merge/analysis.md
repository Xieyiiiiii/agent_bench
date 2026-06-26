# haystack_hybrid_merge Analysis

## Why this passage was extracted

Hybrid RAG retrieval often combines dense and sparse candidate lists. The CPU
workload is not model inference; it is duplicate detection, metadata filtering,
fixed-point score weighting, and top-k selection over a small candidate pool.
`DocumentJoiner` is the closest Haystack component for this behavior.

For CGRA benchmarking, the candidate lists are deterministic stand-ins for CPU
retriever outputs. The kernel evaluates small-table control flow and score
fusion, not full dense/sparse retrieval.

## C implementation target

Preserve:

- two deterministic candidate lists;
- duplicate detection by document ID;
- cross-source duplicate counting for deterministic dense/sparse inputs;
- metadata filter branch;
- weighted dense/sparse score merge;
- overflow counter for fixed-size merged table.

Do not preserve:

- full Haystack join modes;
- Pyserini search stack;
- RRF unless a future benchmark explicitly changes the scoring formula;
- duplicate bonus;
- dynamic document objects.

## CPU bottleneck modeled

This kernel stresses small-table linear search, branch-heavy duplicate and
filter logic, fixed-point score combination, and final top-k update. It is a
control-flow-heavy candidate fusion workload.

## Procedural implementation shape

The implementation should show candidate ingestion and scoring as separate
stages:

```text
main
  init_data
  reset_state
  run_kernel
    merge_candidate_list(dense)
      passes_filter
      insert_or_find_merged
    merge_candidate_list(sparse)
      passes_filter
      insert_or_find_merged
    score_merged_candidates_q8
      weighted_merge_score_q8
    collect_merged_topk
  checksum_result
  print_result
```

Use a single merged-candidate table. Do not maintain parallel duplicate maps or
extra score tables unless the fixed-size linear scan becomes a measured
bottleneck.

## Behavior matching checks

- Filtered candidates must not allocate merged slots.
- Duplicate count must reflect a candidate whose `doc_id` already exists in the
  merged table because it was accepted from another source list.
- v0 scoring must be pure weighted score merge unless a benchmark-only extension
  is explicitly documented.
- Weighted score calculation must match the Q8 contract in `source_excerpt.md`;
  missing dense or sparse scores contribute zero.
- RRF must remain not implemented unless the score is computed from ranks rather
  than source scores.
- The CGRA slice has no `num_docs` parameter, so the host harness must provide
  dense/sparse `doc_id` values that are valid metadata indexes. This slice
  benchmarks merge/filter/duplicate/Top-K control flow, not input validation.

## CGRA single-function boundary

Under the 150-instruction plan, the CGRA version no longer maps the whole fusion
flow into one `hybrid_merge_core.c` file. It is split into
`hybrid_merge_ingest_core.c` and `hybrid_merge_score_topk_core.c`. Together the
two slices keep dense/sparse candidate ingest, metadata filtering, cross-source
duplicate detection, weighted Q8 merge, overflow handling, and Top-K update.
The old oversized `hybrid_merge_core.c` should not remain as a checked `.c`
file after the split.
Host helpers such as
`merge_candidate_list`, `passes_filter`, `find_merged`,
`insert_or_find_merged`, `score_merged_candidates_q8`, and
`collect_merged_topk` are expanded as inline stages:

```text
hybrid_merge_ingest_core
  initialize merged flat table and counters
  assume the host has supplied a valid doc_id window
  ingest dense candidates with filter and linear insert
  ingest sparse candidates with filter, duplicate count, and linear insert
  write merged_count, duplicates, filtered, overflow

hybrid_merge_score_topk_core
  for each merged entry
    compute weighted merged score
    inline Top-K max-score update
  write topk IDs/scores
```

Validation must keep the two slice contracts visible. First compare
`hybrid_merge_ingest_core` against the ingest reference by checking the merged
flat arrays plus `merged_count`, `duplicates`, `filtered`, and `overflow`. Then
feed that same merged table into `hybrid_merge_score_topk_core` and compare
weighted Q8 scores and Top-K ordering. The second slice is not validated by
re-reading the original dense/sparse lists, and it must not modify the merged
flat arrays.

The CGRA form uses flat arrays instead of `Candidate` or `MergedCandidate`
structures. This is a control-flow-heavy candidate-fusion workload and must
keep source branches, filters, duplicate search, overflow, and Top-K update.
The current frontend cannot lower `continue` or `break`, so filter, duplicate,
and overflow paths must use nested `if` blocks. RRF and duplicate bonus remain
not implemented.
