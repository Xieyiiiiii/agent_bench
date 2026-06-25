# Reference Documents

本目录记录 C99 micro-benchmarks 的 reference-first 依据。它不是第三方源码的
vendor 目录，也不是上游框架文档的镜像；它保存的是每个 benchmark 需要追溯的
开源来源、核心伪代码、简化边界、CPU 瓶颈分析和 C 函数映射契约。

这些 reference 服务于 CGRA/CPU co-benchmarking。当前 C 程序不会构建真实 RAG
知识库，不运行完整 Haystack pipeline、document store、tokenizer、embedding model、
PromptBuilder 或 LLM prompt runtime。加速器在这里被视为 CPU 的协助单元，因此每个
reference 都只抽取一个 compact、deterministic 的 workload，用来近似更大检索或
上下文准备系统中的控制流、分支行为、跳转模式、访存和评分循环。

## 每个 kernel 目录包含什么

每个 `reference/<kernel>/` 目录包含三类文件：

- `source_excerpt.md`: upstream repo、源码文件或官方文档 URL、目标函数/类方法、
  reduced pseudocode、benchmark-only extensions、C function mapping contract、
  behavior matching constraints。
- `analysis.md`: 英文分析，说明为什么抽取该片段、C 实现目标、保留与省略内容、
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
- C code 必须匹配这里定义的小核行为，不匹配完整上游应用行为；
- 如果某个近似行为是为了 CGRA 测试加入的，它必须出现在 reference、C 文件头、
  counter 或输出字段中。
- host reference 可以使用 helper 函数链解释完整流程；CGRA 版本必须把对应 helper
  行为内联为单函数阶段块。
- 如果 CGRA 指令预算要求摘取算法片段，reference analysis 必须写明 `CGRA slice
  boundary`，不能让使用者误以为 CGRA 文件实现了完整 host pipeline。

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

CGRA 单函数版本额外遵循 `cgra_flatten_rewrite_plan.md`：单文件单函数、无 helper
call、无 `main`、无 print、输出 buffer 回写，并通过反汇编审查指令数和 call-like 指令。
