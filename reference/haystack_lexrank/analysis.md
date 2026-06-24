# haystack_lexrank Analysis

## Why this passage was extracted

LexRank-style summarization compresses retrieved snippets by building a
sentence similarity graph, ranking nodes, and selecting high-centrality
sentences while avoiding redundancy. This is a clear CPU-side context
compression workload that does not require an LLM or external corpus.

For CGRA benchmarking, the sentence-term matrix is deterministic synthetic data.
The kernel targets pairwise similarity, graph construction, branch-heavy rank
propagation, and selection logic rather than full natural-language
summarization.

## C implementation target

Preserve:

- deterministic sentence-term matrix;
- pairwise sentence similarity;
- threshold graph construction;
- PageRank-style iterative update;
- documented dangling-node handling;
- rank-based selection with redundancy skips.

Do not preserve:

- full tokenizer and stop-word processing;
- exact cosine unless explicitly implemented;
- NetworkX graph API;
- convergence tolerance logic.

## CPU bottleneck modeled

This kernel stresses pairwise similarity computation, dense small-matrix graph
updates, iterative rank propagation, and selection-time redundancy checks. It
models CPU-side extractive compression before prompt construction.

## Procedural implementation shape

The implementation should preserve the LexRank stages directly:

```text
main
  init_corpus
  reset_state
  run_kernel
    compute_similarity_matrix
      sentence_sim_q8
    build_threshold_graph
    run_rank_iterations
    select_summary_sentences
      sentence_is_redundant
  checksum_result
  print_result
```

Keep graph, similarity matrix, rank vectors, and selected IDs in their natural
arrays. Avoid wrapping each matrix in separate single-field structures.

## Behavior matching checks

- Similarity matrix construction must define whether the graph is symmetric or
  directed.
- If modeling LexRank's usual threshold graph, prefer symmetric pair construction
  and count either undirected pairs or directed stored edges explicitly.
- Dangling nodes must be handled by the documented redistribution policy.
- Q8 rank propagation must match the contract in `source_excerpt.md`, including
  no renormalization after fixed iterations.
- Rank initialization, damping, base term, and fixed iteration count must be
  visible in the implementation.
- Redundancy filtering must compare a candidate against already selected
  sentences before insertion.
