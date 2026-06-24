# haystack_lexrank Reference Excerpt

## Upstream sources

- `crabcamp/lexrank`
  - URL: https://github.com/crabcamp/lexrank/blob/dev/README.rst
  - Role: LexRank algorithm overview, similarity-matrix centrality interface,
    and summary/ranking behavior background.
- `sumy` LexRank summarizer
  - URL: https://github.com/miso-belica/sumy/blob/main/sumy/summarizers/lex_rank.py
  - Role: similarity matrix, threshold graph, power-method ranking, sentence
    selection.
- NetworkX PageRank
  - Docs: https://networkx.org/documentation/stable/reference/algorithms/generated/networkx.algorithms.link_analysis.pagerank_alg.pagerank.html
  - Role: damping factor, incoming-edge rank propagation, iterative update.

## Core algorithm passage, reduced to pseudocode

```text
input: sentence-term matrix with term frequencies
for each sentence pair (i, j):
    sim[i][j] = sentence_similarity(i, j)
    if sim[i][j] exceeds threshold:
        add graph edge between i and j
        if modeling an undirected LexRank graph:
            set both graph[i][j] and graph[j][i]
initialize rank[i] = 1 / num_sentences
repeat fixed iteration count:
    dangling_sum = sum(rank[src] for src where out_degree[src] == 0)
    for each destination sentence dst:
        incoming = sum(rank[src] / out_degree[src] for src -> dst
                       where out_degree[src] > 0)
        incoming += dangling_sum / num_sentences
        new_rank[dst] = base + damping * incoming
    rank = new_rank
sort sentences by rank descending
select top sentences, skipping near-duplicates
return selected sentence ids and counters
```

## Notes

The v0 benchmark may use fixed iterations instead of convergence. If the
similarity function is not strict cosine, it must be named generically, e.g.
`sentence_sim_q8()`.

The v0 PageRank-style update must define dangling-node behavior. The preferred
contract is defensive dangling redistribution:

```text
dangling_sum = sum(rank_old[src] for src with out_degree[src] == 0)
incoming = sum(rank_old[src] / out_degree[src] for src -> dst
               where out_degree[src] > 0)
incoming += dangling_sum / num_sentences
rank_new[dst] = base + damping * incoming
```

This is still a simplified fixed-iteration PageRank-style workload and not a
full NetworkX API implementation.

## Benchmark-only extensions

- Deterministic sentence-term matrix replaces tokenization and real text.
- Fixed Q8 similarity and rank arithmetic replace Python floating-point graph
  routines.
- Fixed iteration count replaces convergence tolerance.
- Redundancy filtering is deterministic and based on the benchmark similarity
  matrix.
- The workload approximates CPU-side pairwise similarity, graph traversal, and
  iterative ranking for CGRA benchmarking; it does not perform full text
  summarization.

## Q8 rank and similarity contract

The v0 C benchmark uses Q8 integers and int64 intermediates for similarity and
rank propagation:

```text
Q8_ONE = 256
INITIAL_RANK_Q8 = Q8_ONE / NUM_SENTENCES
base_q8 = (Q8_ONE - damping_q8) / NUM_SENTENCES

rank_old_q8[i] = INITIAL_RANK_Q8
for each fixed iteration:
    dangling_sum_q8 = sum(rank_old_q8[src] where out_degree[src] == 0)
    dangling_share_q8 = dangling_sum_q8 / NUM_SENTENCES
    for each dst:
        incoming_q8 = dangling_share_q8
        for each src with graph[src][dst] and out_degree[src] > 0:
            incoming_q8 += rank_old_q8[src] / out_degree[src]
        rank_new_q8[dst] = base_q8 + q8_mul(damping_q8, incoming_q8)
```

`q8_mul(a_q8, b_q8) = (a_q8 * b_q8) / Q8_ONE`, truncating toward zero. Fixed
iterations do not renormalize ranks; small truncation drift is accepted as part
of the benchmark. Graph construction uses `sim_q8 >= SIM_THRESHOLD_Q8`.

## C function mapping contract

```text
sentence pair similarity           -> sentence_sim_q8
fill similarity matrix             -> compute_similarity_matrix
threshold graph construction       -> build_threshold_graph
fixed PageRank-style iterations    -> run_rank_iterations
redundancy check                   -> sentence_is_redundant
summary sentence selection         -> select_summary_sentences
```

Graph construction must state whether `edges` counts undirected sentence pairs
or directed stored adjacency entries. The preferred v0 implementation is
symmetric storage with a clearly documented counter meaning.

## Behavior matching constraints

- `sentence_sim_q8()` must not be described as strict cosine unless it implements
  strict cosine.
- Graph construction must explicitly document whether `EDGES` counts undirected
  pairs or directed stored adjacency entries.
- `run_rank_iterations()` must handle `out_degree == 0` using the documented
  dangling policy.
- Q8 rank propagation, damping, base, and truncation behavior must match the Q8
  rank and similarity contract above.
- Rank initialization, damping, base term, and fixed iteration count must be
  visible in C.
- Redundancy filtering compares a candidate against already selected sentences
  before insertion.
