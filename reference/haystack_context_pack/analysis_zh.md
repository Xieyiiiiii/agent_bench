# haystack_context_pack 中文分析

## 为什么抽取这段参考行为

RAG 系统通常会在 prompt rendering 前整理 retrieved documents。CPU 工作包括排序、
去重和 token budget packing。Haystack `PromptBuilder` 是 documents 被消费进入 prompt
的位置，但它本身不是 token optimizer。本 benchmark 有意建模 PromptBuilder-adjacent
pre-render preparation，而不是模板渲染。

在 CGRA benchmark 场景中，retrieved document candidates 是 deterministic numeric
records。这个 kernel 近似 prompt context preparation 中的排序、去重和 budget 分支，
不构造真实 prompt，也不调用 LLM。

## C 实现目标

需要保留：

- deterministic candidate list；
- 按 score 做 insertion sort；
- 按 `(source_id, chunk_id)` 去重；
- token budget packing 和 truncation counters。

不需要保留：

- Jinja template rendering；
- 真实字符串或 document content；
- tokenizer-dependent token counting；
- Haystack component lifecycle。

## CPU 瓶颈分析

主要瓶颈是中等候选集合上的排序、嵌套 duplicate check、budget 分支和 packed output
构造。它模拟的是 prompt-context preparation 的 CPU 成本，不模拟 LLM generation。
由于候选数量有限，简单 insertion sort 和线性 duplicate scan 更容易让控制流和数据移动
成本可观察，也符合 micro-benchmark 的可读性目标。

## 面向过程实现形态

函数链应保持 prompt-context preparation 的步骤清晰：

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

只需要一个 candidate array 和一个 packed-output array。不要引入单独的 token-budget
manager object；remaining budget 是简单派生值。

## 行为匹配检查

- 排序必须按 score 降序，分数相同用 `doc_id` deterministic tie-break。
- duplicate detection 基于 `(source_id, chunk_id)`，不是 `doc_id`。
- packed document 的 `used_tokens` 不得超过 remaining token budget。
- truncation 只能在 remaining budget 达到 minimum useful truncation threshold 时发生。
- truncated packed document 的 `used_tokens` 必须等于当时的 remaining token budget。
