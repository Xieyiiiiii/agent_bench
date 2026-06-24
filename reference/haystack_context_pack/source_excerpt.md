# haystack_context_pack Reference Excerpt

## Upstream sources

- Haystack `PromptBuilder.run()`
  - URL: https://github.com/deepset-ai/haystack/blob/main/haystack/components/builders/prompt_builder.py
  - Docs: https://docs.haystack.deepset.ai/docs/promptbuilder
  - Role: consumes variables such as `documents` and `query`, then renders a
    prompt template.
- Haystack RAG pipelines using `PromptBuilder`
  - URL: https://docs.haystack.deepset.ai/docs/promptbuilder
  - Role: retrieved documents are passed as prompt variables before generator
    invocation.

## Core algorithm passage, reduced to pseudocode

```text
input: retrieved document candidates with score, source, chunk, token_len
sort candidates by descending score, tie-break by doc_id
initialize empty packed context and token budget
for each candidate:
    if same source/chunk was already packed:
        count skipped_duplicate
        continue
    if full document fits remaining budget:
        append document with full token_len
        continue
    if remaining budget is enough for a useful truncation:
        append truncated document
        count truncated
    else:
        count skipped_budget
return packed document ids, used token count, counters
```

## Notes

This is PromptBuilder-adjacent context preparation. Haystack's `PromptBuilder`
does template rendering; the score sort, source/chunk deduplication, and token
budget policy are benchmark-defined CPU work before prompt rendering.

## Benchmark-only extensions

- Deterministic candidate metadata replaces real document text.
- Score sorting, `(source_id, chunk_id)` deduplication, token-budget packing,
  and truncation are benchmark-defined policies.
- Token lengths are deterministic integers, not tokenizer output.
- The workload approximates CPU-side context preparation branches for CGRA
  benchmarking; it does not render a prompt or call an LLM.

## Token budget contract

The C benchmark must define these integer constants in the implementation:

```text
TOKEN_BUDGET
MIN_TRUNC_TOKENS
```

Packing behavior is deterministic:

```text
remaining = TOKEN_BUDGET - used_tokens
if token_len <= remaining:
    append with used_tokens = token_len and truncated = 0
elif remaining >= MIN_TRUNC_TOKENS:
    append with used_tokens = remaining and truncated = 1
    count truncated
else:
    count skipped_budget
```

`TOKEN_BUDGET` must be positive. `MIN_TRUNC_TOKENS` must be positive and less
than or equal to `TOKEN_BUDGET`. A truncated packed document consumes all
remaining budget.

## C function mapping contract

```text
sort by descending score           -> sort_candidates_by_score
same source/chunk check            -> is_duplicate_source_chunk
remaining token budget             -> remaining_budget
append full/truncated document     -> append_packed_doc
budget and truncation decision     -> pack_candidate_with_budget
```

The implementation should not introduce string buffers or template-rendering
state. This kernel models numeric context packing only.

## Behavior matching constraints

- Sort by descending score, with smaller `doc_id` tie-break.
- Duplicate detection uses `(source_id, chunk_id)`, not `doc_id`.
- A packed entry's `used_tokens` must never exceed remaining budget.
- Truncation may occur only when remaining budget is at least
  `MIN_TRUNC_TOKENS`; otherwise the candidate is skipped.
- A truncated packed entry must use exactly the remaining token budget.
- Output must include packed IDs, used tokens, skip/truncation counters, and
  checksum.
