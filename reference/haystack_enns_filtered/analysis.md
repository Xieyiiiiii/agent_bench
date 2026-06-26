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

## CGRA single-function boundary

The CGRA version maps to `enns_filtered_core.c`. Under the 150-instruction
target it should use a Top-2 filtered dense retrieval slice while preserving the
core control flow: metadata filter, valid Top-K boundary check, full L2, early
abandon, and Top-K update. Host helpers such as `passes_filter`,
`topk_boundary_is_valid`, `l2_distance_until_cutoff`, and
`update_topk_min_distance` are expanded inline:

```text
enns_filtered_core
  initialize topk and counters
  for each document
    read flat metadata and apply predicate
    count filtered documents without continue
    otherwise enter the distance path
      run full L2 when the Top-2 boundary is invalid
      otherwise run cutoff L2 with early abandon
      insert complete candidates into Top-2
  write top2 and branch counters to out[]
```

The CGRA form uses flat metadata arrays instead of `DocMeta`. The output buffer
must prove filter, full-distance, abandon, and boundary-valid paths. For the
150-instruction slice, the comparison target is Top-2 behavior: either run a
Top-2 reference slice or compare only against the first two entries of the host
Top-4 result. The recommended output layout is `out[0..1]` for Top-2 document
IDs, `out[2..3]` for Top-2 distances, followed by `filtered_out`,
`distance_full`, and `distance_abandoned`. Counters must be compared against a
Top-2 reference slice, not against the Top-4 reference counters, because the
Top-K boundary becomes valid at a different time and may change early-abandon
counts. This is a control-flow-heavy kernel. The 150-instruction plan may
remove defensive counters such as `invalid_boundary_abandon`, but early abandon
and boundary validation must remain. The current frontend cannot lower
`continue` or `break`, so rejected paths must use nested `if` blocks and a
`complete` flag.
