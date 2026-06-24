# haystack_hybrid_merge 中文分析

## 为什么抽取这段参考行为

hybrid RAG retrieval 常见流程是把 dense retriever 和 sparse retriever 的候选结果
合并。这里的 CPU 工作负载不是模型推理，而是小候选集合上的 duplicate detection、
metadata filtering、fixed-point score weighting 和 Top-K selection。Haystack
`DocumentJoiner` 是最接近该行为的组件。

在 CGRA benchmark 场景中，dense/sparse candidate lists 是 CPU retriever 输出的
deterministic 替身。这个 kernel 测试的是小表线性查找、分支和 score fusion，
不是完整 dense/sparse 检索过程。

## C 实现目标

需要保留：

- 两个 deterministic candidate lists；
- 按 `doc_id` 做 duplicate detection；
- 对 deterministic dense/sparse 输入统计跨来源 duplicate；
- metadata filter 分支；
- weighted dense/sparse score merge；
- fixed-size merged table 的 overflow counter。

不需要保留：

- Haystack 完整 join modes；
- Pyserini search stack；
- RRF，除非未来 benchmark 明确改成 rank-based reciprocal scoring；
- duplicate bonus；
- dynamic document objects。

## CPU 瓶颈分析

主要瓶颈是小表线性查找、filter/duplicate 分支、fixed-point score combination
和最后 Top-K 更新。候选规模较小时，线性 merged table 查找是有意保留的工作负载：
它让 duplicate detection 的控制流成本可见，而不是用额外 hash map 隐藏掉。
这个 kernel 是 control-flow-heavy candidate fusion workload。

## 面向过程实现形态

函数链应把候选 ingest 和最终 scoring 分开：

```text
main
  init_data
  reset_state
  run_kernel
    merge_candidate_list(dense)
      passes_filter
      insert_or_find_merged
    merge_candidate_list(sparse)
      passes_filter
      insert_or_find_merged
    score_merged_candidates_q8
      weighted_merge_score_q8
    collect_merged_topk
  checksum_result
  print_result
```

只使用一个 merged-candidate table。v0 不应额外维护 duplicate map 或独立 score table，
除非未来明确把这些数据结构本身作为 benchmark 对象。

## 行为匹配检查

- filtered candidates 不得分配 merged slot。
- `DUPLICATES` 必须表示候选 `doc_id` 已经因为另一个来源列表被接受而存在于 merged table。
- v0 scoring 必须是 pure weighted score merge，除非 benchmark-only extension 被明确记录。
- weighted score 必须匹配 `source_excerpt.md` 中的 Q8 contract；缺失 dense 或 sparse
  分数按 0 处理。
- RRF 必须保持 not implemented；只有当 score 来自 rank reciprocal 而不是 source score 时，
  才能声称实现 RRF。
