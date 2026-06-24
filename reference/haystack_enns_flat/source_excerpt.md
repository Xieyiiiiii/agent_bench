# haystack_enns_flat Reference Excerpt

## Upstream sources

- Haystack `InMemoryEmbeddingRetriever.run()`
  - URL: https://github.com/deepset-ai/haystack/blob/main/haystack/components/retrievers/in_memory/embedding_retriever.py
  - Role: accepts `query_embedding`, optional runtime `top_k`, and optional
    filters, then delegates retrieval to the in-memory document store.
- Haystack in-memory document store embedding retrieval
  - URL: https://github.com/deepset-ai/haystack/blob/main/haystack/document_stores/in_memory/document_store.py
  - Role: scores in-memory documents by embedding similarity and returns the
    highest-ranked documents.
- FAISS `IndexFlat` / `IndexFlatL2` search
  - URL: https://github.com/facebookresearch/faiss/blob/main/faiss/IndexFlat.cpp
  - API docs: https://faiss.ai/cpp_api/structfaiss_1_1IndexFlatL2.html
  - Role: exhaustive flat vector search returning distances and labels.
- FAISS heap utilities
  - URL: https://github.com/facebookresearch/faiss/blob/main/faiss/utils/Heap.h
  - Role: maintain top-k labels/scores during search.

## Core algorithm passage, reduced to pseudocode

```text
input: query vector q, document vectors db[0..N), k
initialize top-k result slots to invalid / worst distance
for each document id d:
    distance = squared_l2(q, db[d])
    if distance improves current top-k boundary:
        insert (d, distance) into sorted top-k slots
return top-k document labels and distances
```

## Notes

Haystack supplies the retriever API shape and in-memory retrieval flow. FAISS
supplies the exact exhaustive L2 search semantics. The C benchmark intentionally
uses deterministic insertion-based top-k instead of FAISS heap code.

## Benchmark-only extensions

- Deterministic synthetic int16 vectors replace real embeddings and document
  objects.
- Deterministic insertion Top-K replaces FAISS heap utilities.
- The workload is a CGRA-friendly small kernel that approximates dense
  retrieval's CPU-side vector scan; it does not build or query a real RAG
  knowledge base.

## C function mapping contract

```text
initialize top-k result slots      -> reset_result
for each document id d             -> run_kernel
squared_l2(q, db[d])               -> l2_distance
insert into sorted top-k slots     -> update_topk_min_distance
return labels/distances/checksum   -> print_result / checksum_result
```

The implementation should not add a `State` structure unless a future benchmark
stage creates real intermediate data.

## Behavior matching constraints

- Every document must be scanned exactly once.
- Top-K order is ascending by squared L2 distance.
- Equal distances use smaller `doc_id` as the deterministic tie-break.
- Output must include valid Top-K IDs, distances, `DOCS_SCANNED`, and checksum.
