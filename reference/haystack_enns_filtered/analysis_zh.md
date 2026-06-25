# haystack_enns_filtered 中文分析

## 为什么抽取这段参考行为

filtered dense retrieval 同时包含两个常见 CPU 模式：metadata predicate 分支和
dense vector scoring。Haystack 提供 filter-before-retrieval 的行为边界，FAISS
提供 exhaustive L2 scoring 的向量计算语义。本 benchmark 把二者组合成可控的
branch-heavy dense retrieval workload。

在 CGRA benchmark 场景中，这个 kernel 近似 filtered retrieval 中复杂分支、
cutoff 判断、部分向量累加和 Top-K 更新的 CPU 热路径。它不执行真实 Haystack
filter language 或 document store，只保留适合芯片小核测试的控制流和算术模式。

## C 实现目标

需要保留：

- 距离计算前先执行 deterministic metadata predicate；
- 统计被 filter 跳过的文档；
- 对通过 filter 的文档执行 L2 scan；
- 维护 Top-K IDs 和 scores；
- 明确标注 early abandon 是 benchmark-only extension。

正确性约束：

- 只有当 Top-K 已经填满、最差 Top-K distance 有意义时，才能启用 early abandon。
  否则候选可能在结果还不足 Top-K 时被错误丢弃。

不需要保留：

- Haystack filter expression parser；
- 任意嵌套 metadata 条件；
- FAISS index 内部结构；
- Python object allocation。

## CPU 瓶颈分析

主要瓶颈来自 filter selectivity、branch prediction、部分向量累加和 Top-K 边界判断。
和 flat scan 不同，这个 kernel 的成本不只是 `NUM_DOCS * DIM` 算术量，还包括
filter 分支是否稳定、通过 filter 的文档比例、early abandon 是否提前终止距离计算。
它模拟的是控制流和算术混合的 retrieval workload。

## 面向过程实现形态

函数链应显式表达 filter -> score -> Top-K：

```text
main
  init_data
  reset_result
  run_kernel
    passes_filter
    topk_boundary_is_valid
    l2_distance 或 l2_distance_until_cutoff
    update_topk_min_distance
  checksum_result
  print_result
```

metadata 保持在一个紧凑的 `DocMeta` 数组中，不要为每个 predicate 字段再拆一层结构。

## 行为匹配检查

- 被 filter 拒绝的文档不得进入距离计算。
- Top-K 未填满前必须走完整 L2 distance。
- early abandon 只能在 partial L2 已经超过有效 Top-K worst distance 时拒绝候选。
- counters 必须证明 filtered、full distance、abandoned distance 和 valid Top-K 路径都被触发。

## CGRA 单函数实现边界

CGRA 版本对应 `enns_filtered_core.c`，优先完整保留 filtered dense retrieval 的复杂控制流：
metadata filter、Top-K boundary 有效性、完整 L2、early abandon 和 Top-K 更新。host
版本中的 `passes_filter`、`topk_boundary_is_valid`、`l2_distance_until_cutoff`、
`update_topk_min_distance` 等 helper 必须在单函数内直接展开。

```text
enns_filtered_core
  初始化 topk 和 counters
  遍历 document
    读取 flat meta 字段并执行 predicate
    如果 filter 失败，增加 filtered_out 并 continue
    如果 Top-K boundary 无效，完整 L2
    否则按 cutoff 做 partial L2 和 early abandon
    完整候选进入内联 Top-K 插入
  写回 topk、filtered_out、distance_full、distance_abandoned、invalid_boundary_abandon
```

CGRA 版本不使用 `DocMeta` 结构体，metadata 使用 flat `int meta[]`。输出 buffer 必须证明
filter、full distance、abandon 和 boundary-valid 路径都被触发。该 slice 是
control-flow-heavy kernel，不能为了压低指令数删除 early abandon 或 boundary 检查；若
最终因指令数超限删减，必须在 mapping 中降低其分支覆盖声明。
