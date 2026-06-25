# haystack_hybrid_merge Reference Excerpt

## Upstream sources

- Haystack `DocumentJoiner`
  - URL: https://github.com/deepset-ai/haystack/blob/main/haystack/components/joiners/document_joiner.py
  - Docs: https://docs.haystack.deepset.ai/docs/documentjoiner
  - Role: joins multiple document lists and handles duplicate documents through
    configured join modes.
- Haystack hybrid retrieval tutorial
  - URL: https://haystack.deepset.ai/tutorials/33_hybrid_retrieval
  - Role: dense and sparse retriever outputs are joined before downstream RAG.
- Pyserini hybrid / RRF references
  - URL: https://github.com/castorini/pyserini/blob/master/README.md
  - Role: rank fusion background only. v0 does not implement Pyserini search.

## Core algorithm passage, reduced to pseudocode

```text
input: dense candidates, sparse candidates, metadata, k
initialize empty merged candidate table
for each candidate in dense list:
    if metadata filter rejects doc_id:
        count filtered
        continue
    insert doc_id or find existing merged slot
    store dense score and has_dense flag
for each candidate in sparse list:
    if metadata filter rejects doc_id:
        count filtered
        continue
    insert doc_id or find existing merged slot
    if slot already exists:
        count duplicate
    store sparse score and has_sparse flag
for each merged slot:
    score = dense_weight * dense_score + sparse_weight * sparse_score
    update max-score top-k
return merged top-k and counters
```

## Notes

The default v0 implementation target is Haystack-style weighted score merge.
RRF is a documented alternative and must be listed as not implemented unless a
future kernel explicitly computes rank-based reciprocal scores.

For v0, synthetic dense and sparse lists must not contain duplicate IDs within
the same source list. `DUPLICATES` counts cross-source duplicates: a sparse
candidate whose `doc_id` already exists because it was accepted from the dense
list. If future inputs allow same-source duplicates, this reference must be
updated before changing the C behavior.

## Benchmark-only extensions

- Deterministic dense and sparse candidate lists replace real retrievers.
- Fixed metadata arrays replace Haystack `Document` objects and filters.
- Fixed-size linear merged table is intentional: it approximates small
  candidate-fusion control flow for CGRA benchmarking.
- v0 uses pure weighted score merge and does not apply duplicate bonus.
- RRF is not implemented.

## Weighted score contract

Host reference hybrid merge uses Q8 weights and int64 intermediates:

```text
Q8_ONE = 256
dense_weight_q8 + sparse_weight_q8 = Q8_ONE
q8_mul(a_q8, b_q8) = (a_q8 * b_q8) / Q8_ONE

dense_component_q8 = has_dense ? q8_mul(dense_weight_q8, dense_score_q8) : 0
sparse_component_q8 = has_sparse ? q8_mul(sparse_weight_q8, sparse_score_q8) : 0
merged_score_q8 = dense_component_q8 + sparse_component_q8
```

Division truncates toward zero. Missing source scores contribute zero. The
`score_merged_candidates_q8()` stage writes `merged_score_q8` for every merged
slot, and `collect_merged_topk()` ranks by `merged_score_q8` descending with
smaller `doc_id` as the deterministic tie-break.

CGRA single-function slices must preserve weighted-merge semantics, missing
source = 0 behavior, and deterministic tie-breaks. They may use bounded 32-bit
arithmetic if required to avoid backend runtime helper calls, but any such
narrowing must be documented in the CGRA slice boundary and verified by
disassembly.

## C function mapping contract

```text
iterate dense candidates           -> merge_candidate_list(..., SOURCE_DENSE)
iterate sparse candidates          -> merge_candidate_list(..., SOURCE_SPARSE)
metadata filter rejects doc        -> passes_filter
find existing merged slot          -> find_merged
insert or reuse merged slot        -> insert_or_find_merged
score all merged slots             -> score_merged_candidates_q8
per-slot weighted final score      -> weighted_merge_score_q8
final result selection             -> collect_merged_topk
```

Do not introduce a separate duplicate map for v0. The fixed-size merged table
and linear lookup are part of the intended CPU workload.

## Behavior matching constraints

- Filtered candidates must not allocate merged slots.
- `DUPLICATES` counts only accepted cross-source duplicate document IDs in v0.
- Weighted score is computed from source scores, not ranks.
- Missing source scores contribute zero to weighted merge.
- Top-K uses deterministic score ordering with smaller `doc_id` tie-break.
- `OVERFLOW` is printed even when synthetic v0 data is expected to avoid it.
