# haystack_lexrank 中文分析

## 为什么抽取这段参考行为

LexRank-style summarization 通过构造 sentence similarity graph、对句子节点做中心性排名、
再选择高分且不冗余的句子来压缩 retrieved snippets。这是一个明确的 CPU-side context
compression workload，不需要 LLM、外部语料或真实 tokenizer。

在 CGRA benchmark 场景中，sentence-term matrix 是 deterministic synthetic data。
这个 kernel 测试的是两两相似度、图构造、带分支的 rank propagation 和选择逻辑，
不是完整自然语言摘要。

## C 实现目标

需要保留：

- deterministic sentence-term matrix；
- pairwise sentence similarity；
- threshold graph construction；
- PageRank-style iterative update；
- 明确定义 dangling-node 处理；
- rank-based selection 和 redundancy skips。

不需要保留：

- 完整 tokenizer 和 stop-word processing；
- strict cosine，除非实现中明确采用 strict cosine；
- NetworkX graph API；
- convergence tolerance logic。

## CPU 瓶颈分析

主要瓶颈是句子两两相似度计算、小型 dense matrix/graph 更新、迭代式 rank propagation
和 selection 阶段的 redundancy checks。相似度阶段通常是 `MAX_SENTENCES^2` 级别，
rank 阶段是 `ITERATIONS * edges` 或 dense adjacency 扫描成本。这个 kernel 衡量
extractive compression 的 CPU 成本，而不是 prompt rendering 或 LLM inference。

## 面向过程实现形态

函数链应直接对应 LexRank 阶段：

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

graph、similarity matrix、rank vectors 和 selected IDs 保持自然数组即可。
不要为每个 matrix 包一层单字段结构。

## 行为匹配检查

- similarity matrix construction 必须说明 graph 是 symmetric 还是 directed。
- 如果建模 LexRank 常见 threshold graph，优先使用 symmetric pair construction，
  并明确 `EDGES` 统计的是 undirected pairs 还是 directed stored edges。
- dangling nodes 必须按文档定义的 redistribution policy 处理。
- Q8 rank propagation 必须匹配 `source_excerpt.md` 中的 contract，包括 fixed
  iterations 后不做 renormalization。
- rank initialization、damping、base term、fixed iteration count 必须在实现中可见。
- redundancy filtering 必须在插入候选前，把 candidate 与已选择句子比较。

## CGRA 单函数实现边界

完整 LexRank pipeline 很可能超过理论 576 条指令上限，而当前 CGRA 实施目标是更严格的
150 条 practical budget，因此 CGRA 版本默认拆分。第一阶段 `lexrank_rank_core.c`
只匹配 `source_excerpt.md` 中的 PageRank-style iterative update block，不匹配完整
LexRank pipeline，也不负责 similarity matrix、threshold graph 或 redundancy
selection。

```text
lexrank_rank_core
  读取 graph、out_degree、rank_old_q8
  固定迭代次数
    统计 dangling_sum
    对每个 dst 扫描 incoming edges
    执行 Q8 damping update
    rank_new_q8 写回 rank_old_q8
  写回 iterations、dangling_sources_seen
```

如需覆盖完整 LexRank，应拆分为 `lexrank_sim_graph_core.c`、`lexrank_rank_core.c` 和
`lexrank_select_core.c`，每个文件都必须单函数、无调用，并在 mapping 中写清对应的
reference 伪代码块。`lexrank_rank_core.c` 是 control-flow-heavy slice，复杂分支来自
dangling node detection、incoming edge scan 和 fixed rank iteration。
