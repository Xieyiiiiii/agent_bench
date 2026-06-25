# haystack_context_pack Analysis

## Why this passage was extracted

RAG systems often transform retrieved documents before prompt rendering. The
CPU work includes ordering, deduplication, and token-budget packing. Haystack's
`PromptBuilder` is the pipeline point where documents are consumed, but it is
not a token optimizer; this benchmark intentionally models the adjacent
pre-render preparation.

For CGRA benchmarking, retrieved document candidates are deterministic numeric
records. The kernel approximates CPU-side context preparation branches without
building real prompts or calling an LLM.

## C implementation target

Preserve:

- deterministic candidate list;
- insertion sort by score;
- source/chunk deduplication;
- token budget packing and truncation counters.

Do not preserve:

- Jinja template rendering;
- real strings or document content;
- tokenizer-dependent token counting;
- Haystack component lifecycle.

## CPU bottleneck modeled

This kernel stresses sort cost over a moderate candidate set, nested duplicate
checks, budget branches, and compact output construction. It is intended to
model prompt-context preparation, not LLM generation.

## Procedural implementation shape

The implementation should keep prompt-context preparation readable:

```text
main
  init_data
  reset_result
  run_kernel
    sort_candidates_by_score
    is_duplicate_source_chunk
    pack_candidate_with_budget
      append_packed_doc
  checksum_result
  print_result
```

Use one candidate array and one packed-output array. Do not introduce a separate
token-budget manager object; remaining budget is a simple derived value.

## Behavior matching checks

- Sorting must be descending by score with deterministic `doc_id` tie-breaks.
- Duplicate detection is based on `(source_id, chunk_id)`, not `doc_id`.
- A packed document must never exceed the remaining token budget.
- Truncation must happen only when the remaining budget is at least the minimum
  useful truncation threshold.
- A truncated packed document must consume exactly the remaining token budget,
  according to the token budget contract in `source_excerpt.md`.
- The CGRA slice has fixed capacity `CGRA_CONTEXT_K`. If caller-provided `count`
  is larger, the slice processes only the first `CGRA_CONTEXT_K` candidates; any
  extra candidates are host-side window selection or overflow responsibility.

## CGRA single-function boundary

The CGRA version maps to `context_pack_core.c` and keeps score ordering,
source/chunk duplicate skip, token-budget full append, truncation, and
skip-budget branches. Host helpers such as `sort_candidates_by_score`,
`is_duplicate_source_chunk`, `pack_candidate_with_budget`, and
`append_packed_doc` are expanded inline:

```text
context_pack_core
  initialize packed ids, used_tokens, counters
  clamp count to the fixed-capacity candidate window
  order candidates by score
  for each candidate
    check source/chunk duplicate
    compute remaining budget
    take full append / truncate / skip-budget branch
  write packed ids and branch counters
```

If full insertion sort exceeds the instruction budget, a fixed small-K
selection pass may replace it, but the score-ordering semantics and tie-break
must remain documented. This is a control-flow-heavy context-preparation slice;
the output buffer must prove duplicate skip, truncation, or budget skip.
