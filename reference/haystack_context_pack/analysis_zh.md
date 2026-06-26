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
- host reference 可以使用较大的 deterministic candidate list；150 指令 CGRA slice
  可以把固定容量窗口缩小为 `CGRA_CONTEXT_K = 4`。进入 slice 的输入必须已经是 host
  预选或裁剪后的窗口。当外部传入 `count` 更大时，只处理文档化窗口，额外候选属于
  host 侧窗口选择/overflow 责任。host 预选窗口必须 deterministic，测试应固定窗口
  内容和输入顺序。不能把较大 host candidate list 的最终 packing 结果直接作为该 slice
  的逐项匹配目标。

## CGRA 单函数实现边界

CGRA 版本对应 `context_pack_core.c`，保留 score ordering、source/chunk duplicate skip、
token budget full append、truncate 和 skip-budget 分支。host 版本中的
`sort_candidates_by_score`、`is_duplicate_source_chunk`、`pack_candidate_with_budget`、
`append_packed_doc` 必须在单函数中以内联阶段展开。

```text
context_pack_core
  初始化 packed ids、used_tokens、counters
  将 count 收敛到 4 个候选的固定容量窗口
  按 score 选择或排序候选
  对每个候选
    检查 source/chunk duplicate
    计算 remaining budget
    full append / truncate / skip budget 三路分支
  写回 packed ids、used_tokens、truncated、skipped_duplicate、skipped_budget
```

如果完整 insertion sort 导致指令超限，可以改成固定小 K selection pass，但必须仍然保留
score ordering 的 reference 语义和 tie-break。reference 对比必须使用和 CGRA slice
相同的固定窗口，然后在窗口内检查 ordering、source/chunk duplicate、full append、
truncate 和 budget skip。实现代码必须保留 duplicate skip、full append、truncate 和
budget skip 四类路径；测试集必须覆盖这些路径，即使单个输入不同时触发全部分支。
该 slice 是 control-flow-heavy context preparation workload；单次 output buffer 证明
当前输入实际触发的路径，完整测试集证明 duplicate skip、full append、truncate 和
budget skip 都被覆盖。当前编译器前端不支持 `continue`/`break`，因此 duplicate skip、
budget skip 和 no-best path 必须用嵌套 `if` 表达。为压到 150 条指令，可以减少临时
packed-doc 数组搬移，直接写 `out[]`，但不能删除 duplicate 或 budget 分支。
