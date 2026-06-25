# haystack_enns_flat Analysis

## Why this passage was extracted

The relevant CPU workload is the dense retrieval baseline: a query embedding is
compared against every document embedding, and a small top-k result set is kept.
That path is the simplest common denominator behind Haystack in-memory
embedding retrieval and FAISS flat L2 search.

For the CGRA benchmark, this is intentionally a small kernel rather than a real
RAG retrieval stack. It preserves the tight vector-scan loop and top-k branch so
the accelerator can be evaluated as a CPU-side helper.

## C implementation target

Preserve:

- contiguous document embedding scan;
- per-dimension integer L2 accumulation;
- deterministic top-k update and tie-breaking;
- valid top-k IDs plus checksum output.

Do not preserve:

- Haystack `Document` objects;
- dynamic Python metadata structures;
- FAISS class hierarchy, SIMD kernels, or heap implementation;
- real embedding model output.

## CPU bottleneck modeled

This kernel is memory-bandwidth and arithmetic-throughput heavy. The hot path is
the nested loop over `NUM_DOCS * DIM`, with predictable control flow and a small
top-k maintenance branch. It is intended to measure dense vector scan cost, not
framework dispatch cost.

## Procedural implementation shape

The implementation should stay flat and procedural:

```text
main
  init_data
  reset_result
  run_kernel
    l2_distance
    update_topk_min_distance
  checksum_result
  print_result
```

No `State` object is needed unless future logic adds multi-stage intermediate
data. `Input`, `Result`, and `Counters` are enough.

## Behavior matching checks

- Every document must be scanned exactly once.
- `docs_scanned` must equal `NUM_DOCS`.
- Top-K ordering must be ascending by L2 distance with deterministic doc-id
  tie-breaks.
- The C output must expose IDs, distances, and checksum so the exhaustive scan
  behavior can be regression-tested.

## CGRA single-function boundary

The CGRA version maps to `enns_flat_core.c` and keeps only exhaustive L2 scan
and Top-K update. Host functions such as `init_data`, `reset_result`,
`l2_distance`, `update_topk_min_distance`, `checksum_result`, and
`print_result` do not exist as calls in the CGRA file; their behavior is
expanded inline inside one function:

```text
enns_flat_core
  initialize topk ids / distances
  for each document
    accumulate squared L2 over dimensions
    inline Top-K insertion and doc_id tie-break
  write ids, distances, docs_scanned to out[]
```

Input vectors are prepared by the host harness. The output buffer must expose
Top-K IDs, Top-K distances, and `docs_scanned`. This slice is a dense-scan
baseline with low branch complexity; it covers nested loops and Top-K insertion
branches, not a control-flow-heavy workload.
