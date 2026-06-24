# haystack_enns_filtered Reference Excerpt

## Upstream sources

- Haystack `InMemoryEmbeddingRetriever.run()`
  - URL: https://github.com/deepset-ai/haystack/blob/main/haystack/components/retrievers/in_memory/embedding_retriever.py
  - Role: accepts `query_embedding`, `top_k`, and filters, then delegates to the
    document store.
- Haystack in-memory document store filtering and embedding retrieval
  - URL: https://github.com/deepset-ai/haystack/blob/main/haystack/document_stores/in_memory/document_store.py
  - Role: applies metadata filters and retrieves from the selected documents.
- FAISS `IndexFlat` / `IndexFlatL2` search
  - URL: https://github.com/facebookresearch/faiss/blob/main/faiss/IndexFlat.cpp
  - API docs: https://faiss.ai/cpp_api/structfaiss_1_1IndexFlatL2.html
  - Role: exhaustive L2 distance computation and top-k labels/distances.

## Core algorithm passage, reduced to pseudocode

```text
input: query vector q, document vectors db[0..N), metadata meta[0..N), k
initialize top-k result slots
for each document id d:
    if metadata filter rejects meta[d]:
        count filtered_out
        continue
    distance = squared_l2(q, db[d])
    update top-k with (d, distance)
return top-k and filter counters
```

## Benchmark-only extensions

```text
if top-k is already full:
    while accumulating distance:
        if partial distance exceeds current top-k boundary:
            stop this distance computation early
            count distance_abandoned
            reject candidate
```

Early abandon is not claimed as Haystack or FAISS behavior. It is included to
stress branch-heavy CPU behavior after metadata filtering.

Additional benchmark constraints:

- deterministic synthetic metadata replaces real Haystack filter expressions;
- the workload approximates CPU-side filter and vector scoring branches for
  CGRA benchmarking, not a full document-store retrieval operation;
- synthetic data must trigger filtered, full-distance, and abandoned-distance
  paths in one deterministic run.

## C function mapping contract

```text
metadata filter rejects meta[d]    -> passes_filter
full squared_l2(q, db[d])          -> l2_distance
top-k boundary validity check      -> topk_boundary_is_valid
partial distance cutoff            -> l2_distance_until_cutoff
accepted candidate top-k update    -> update_topk_min_distance
filtered/full/abandoned counters   -> EnnsFilteredCounters
```

`l2_distance_until_cutoff` may only be called when the Top-K boundary is valid.
Until then, the implementation must call the full `l2_distance` path.

## Behavior matching constraints

- A document rejected by `passes_filter` must not enter distance computation.
- `topk[TOP_K - 1].doc_id >= 0` is the required validity condition before using
  `topk[TOP_K - 1].score` as a cutoff.
- `DISTANCE_FULL` counts completed distance calculations.
- `DISTANCE_ABANDONED` counts candidates rejected by partial L2 cutoff.
- Top-K IDs must be valid after the run.
