# Reference Documents

本目录记录 C99 micro-benchmarks 的 reference-first 依据。它不是第三方源码的
vendor 目录，也不是上游框架文档的镜像；它保存的是每个 benchmark 需要追溯的
开源来源、核心伪代码、简化边界、CPU 瓶颈分析和 C 函数映射契约。

这些 reference 服务于 CGRA/CPU co-benchmarking。当前 C 程序不会构建真实 RAG
知识库，不运行完整 Haystack pipeline、document store、tokenizer、embedding model、
PromptBuilder 或 LLM prompt runtime。加速器在这里被视为 CPU 的协助单元，因此每个
reference 必须先说明所选 CPU 环节在上游流程中的作用和性能意义，再说明完整系统为何
不能直接放入当前加速器。固定数据、整数运算和单函数形式只用于形成可重复对照，不能
反过来作为 workload 重要性的依据。

## 每个 kernel 目录包含什么

每个 `reference/<kernel>/` 目录包含三类文件：

- `source_excerpt.md`: upstream repo、源码文件或官方文档 URL、目标函数/类方法、
  reduced pseudocode、benchmark-only extensions、C function mapping contract、
  behavior matching constraints。
- `analysis.md`: 英文分析，说明 workload 重要性、简化原因、C 实现目标、保留与省略内容、
  CPU bottleneck、过程式函数形态和行为检查。
- `analysis_zh.md`: 中文分析，面向项目使用者和 code agent，必须保留与英文分析一致
  的瓶颈判断、实现边界和行为匹配约束。

跨 kernel 的审计索引在 `../ref/kernel_reference_mapping.md`。该文件说明每个 C
benchmark 对应哪些 reference、哪些语义来自上游、哪些行为是 benchmark-defined policy。

## 维护顺序

修改任一 benchmark 行为时，按以下顺序维护：

1. 更新 `source_excerpt.md`，先锁定 upstream 来源、伪代码和行为约束。
2. 更新 `analysis.md` 和 `analysis_zh.md`，说明瓶颈分析和过程式实现形态。
3. 更新 `../ref/kernel_reference_mapping.md`，同步实现边界。
4. 更新对应 `src/<kernel>.c` 文件头和 C 实现。
5. 更新 `tests/check_outputs.sh`，让新增或修改后的关键行为可验证。

## 必须保持清楚的边界

- upstream projects 定义算法形态和真实 CPU-side workload 来源；
- benchmark documents 定义 deterministic synthetic data、fixed-point arithmetic、
  truncation policy、early abandon、token budget 等近似行为；
- C code 必须匹配这里定义的 benchmark 行为，不匹配完整上游应用行为；
- 如果某个近似行为是为了 CGRA 测试加入的，它必须出现在 reference、C 文件头、
  counter 或输出字段中。
- host reference 可以使用 helper 函数链解释完整流程；CGRA 版本必须把对应 helper
  行为内联为单函数阶段块。
- 如果 CGRA 指令预算要求缩小规模或拆分算法阶段，reference analysis 必须写明该阶段
  的系统作用、简化原因、保留行为和 `CGRA slice boundary`，不能让使用者误以为 CGRA
  文件实现了完整 host pipeline。

推荐的 C 形态是简单过程式调用链：

```text
main
  init_data / init_index / init_query
  reset_state / reset_result
  run_kernel
    algorithm-named helpers
  checksum_result
  print_result
```

避免把 benchmark 写成小框架，也避免为每个数组或标量拆出单字段结构体。

CGRA 单函数版本额外遵循 `../cgra_kernels/README.md` 和
`../ref/kernel_reference_mapping.md`：单文件单函数、150 条反汇编指令 practical target、
无 helper call、无 `main`、无 print、无 `continue`/`break`、输出 buffer 回写，并通过
反汇编审查指令数和 call-like 指令。

## Scheduling 与 Robotics 扩展

本仓库还包含：

- [`SCHEDULING_ROBOTICS_BACKGROUND_ZH.md`](../SCHEDULING_ROBOTICS_BACKGROUND_ZH.md)
- [`SCHEDULING_ROBOTICS_CODE_SUMMARY_ZH.md`](../SCHEDULING_ROBOTICS_CODE_SUMMARY_ZH.md)
- [`reference_papers/`](../reference_papers/)

前两份文档分别说明 scheduler / robotics 的选择理由和当前实现状态。新增的
`agent_workflow_schedule` 与 `robot_motion_collision` 分别对应
Agent 异构工作流调度和机器人 edge collision checking；动态 MRTA 仅作为后续调研方向，
不在当前 C benchmark 的实现范围内。
