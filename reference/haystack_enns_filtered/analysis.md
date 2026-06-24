# haystack_enns_filtered Analysis

## Why this passage was extracted

Filtered dense retrieval combines two CPU-side patterns that often appear in
RAG systems: metadata predicates and dense vector scoring. Haystack supplies the
filter-before-retrieval behavior, while FAISS supplies the exhaustive L2 scoring
model used by this benchmark.

For CGRA benchmarking, this kernel approximates the branch-heavy part of a
filtered retrieval workload. It does not execute a real metadata filter language
or document store; it keeps the predicate, cutoff branch, partial accumulation,
and top-k update behavior that matter for CPU-side cooperation tests.

## C implementation target

Preserve:

- deterministic metadata predicate before distance computation;
- skipped-document counter;
- L2 scan over accepted documents;
- top-k IDs and scores;
- an explicitly labeled early-abandon extension.

Correctness constraint:

- early abandon may only be enabled after the top-k boundary is meaningful,
  i.e. after all top-k slots are valid. Otherwise a candidate could be skipped
  before the benchmark has enough valid results.

Do not preserve:

- Haystack filter expression parser;
- arbitrary nested metadata conditions;
- FAISS index internals;
- Python object allocation.

## CPU bottleneck modeled

This kernel stresses branch prediction, filter selectivity, partial vector
accumulation, and top-k boundary checks. It is intended to model a mixed
control-flow and arithmetic workload rather than a pure vector scan.

## Procedural implementation shape

The implementation should make the two-stage filter-and-score flow explicit:

```text
main
  init_data
  reset_result
  run_kernel
    passes_filter
    topk_boundary_is_valid
    l2_distance or l2_distance_until_cutoff
    update_topk_min_distance
  checksum_result
  print_result
```

Keep metadata in one compact `DocMeta` array. Do not create separate structures
for each predicate field.

## Behavior matching checks

- Filtered-out documents must not enter distance computation.
- Full distance computation must be used until all Top-K slots are valid.
- Early abandon may only reject a candidate when partial L2 distance already
  exceeds a valid worst Top-K distance.
- Counters must prove all key paths ran: filtered, full distance, abandoned
  distance, valid Top-K.
