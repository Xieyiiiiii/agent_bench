# haystack_enns_flat 中文分析

## 为什么抽取这段参考行为

这个 kernel 对应 dense retrieval 的最小 CPU 工作负载：一个 query embedding
和所有 document embeddings 做逐个比较，并维护一个很小的 Top-K 结果集。
Haystack 提供 in-memory embedding retrieval 的调用语义，FAISS flat L2 search
提供 exhaustive scan + L2 distance + labels/distances 的核心检索语义。

在 CGRA benchmark 场景中，这不是完整 RAG 检索栈，而是可灌入芯片的小核。
它保留连续向量扫描和 Top-K 更新分支，用来评估加速器作为 CPU 协助单元时对
这类热点循环的处理能力。

## C 实现目标

需要保留：

- 连续扫描所有 document embedding；
- 每个维度做整数 L2 累加；
- deterministic Top-K 更新和 doc_id tie-break；
- 输出有效 Top-K IDs、scores 和 checksum。

不需要保留：

- Haystack `Document` 对象；
- Python 动态 metadata 结构；
- FAISS C++ class hierarchy、SIMD kernel 或 heap 实现；
- 真实 embedding model 输出。

## CPU 瓶颈分析

主要瓶颈是内存带宽和整数算术吞吐。热路径是 `NUM_DOCS * DIM` 的双层循环：
每个 document 都要读取一整条向量并做差值平方累加。控制流基本可预测，
只有 Top-K 边界更新分支会带来少量 branch cost。这个 kernel 衡量的是 dense
vector full scan 成本，不衡量 Haystack/FAISS 框架调度成本。

## 面向过程实现形态

函数链应保持直接：

```text
main
  init_data
  reset_result
  run_kernel
    l2_distance
    update_topk_min_distance
  checksum_result
  print_result
```

这个 kernel 不需要 `State` 结构。`Input + Result + Counters` 足够表达数据和输出。

## 行为匹配检查

- 每个 document 必须且只扫描一次。
- `docs_scanned` 必须等于 `NUM_DOCS`。
- Top-K 必须按 L2 distance 升序排列，分数相同用较小 `doc_id` tie-break。
- 输出必须包含 IDs、distances 和 checksum，便于回归验证 exhaustive scan 行为。
