# Agentic AI / RAG CPU-side C Benchmark

本仓库提供一组小规模、确定性、可校验的 C99 benchmark，用于评估
agentic AI / RAG 系统中常见的 CPU-side 小核。它面向 CGRA/CPU 协同测试：
CGRA 作为 CPU 的协助单元承接可抽取的小场景，而不是独立运行完整 RAG 系统。

当前代码不会构建真实知识库，不运行 Haystack pipeline，不调用 tokenizer、
embedding model、PromptBuilder、LLM API、GPU API 或第三方检索库。每个 kernel
都从开源项目中抽取可追溯的 workload 语义，再用 deterministic synthetic data
近似循环、复杂分支、跳转、访存、partial accumulation、fixed-point scoring、
score fusion 和 Top-K 等 CPU 行为。

## 快速使用

```bash
make test
```

`make test` 会执行完整验证：

1. 用 `-std=c99 -Wall -Wextra -Werror` 编译全部 C benchmark；
2. 运行 6 个可执行文件并生成 `build/*.out`；
3. 检查 reference 文档、C 文件头、关键 counter、checksum 和行为约束。

手动拆分执行时，顺序必须是：

```bash
make all
bash tests/run_all.sh
bash tests/check_outputs.sh
```

清理运行生成物：

```bash
make clean
```

## 目录结构

```text
PROJECT_OVERVIEW.md                项目背景、benchmark 选择理由、当前状态
agent_cpu_c_bench_initial_plan.md  reference-first 实施计划和实现约束
README.md                          使用入口
Makefile                           构建、测试、清理入口

reference/                         参考来源、伪代码、瓶颈分析
ref/kernel_reference_mapping.md    reference 到 C 实现边界的中文索引
include/                           公共配置、Top-K、Q8、checksum helper
src/                               6 个独立 C benchmark
cgra_kernels/                      CGRA 单函数 kernel slice，按计划新增
scripts/                           指令数审查脚本，按计划新增
tests/                             输出生成和一致性检查脚本
```

`build/` 是运行生成目录，不属于源文件；可随时通过 `make clean` 删除。

## Kernel 列表

| Kernel | 建模目标 | 关键输出 |
|---|---|---|
| `haystack_enns_flat` | dense retrieval full scan + L2 Top-K | `TOPK_IDS`, `TOPK_SCORES`, `DOCS_SCANNED`, `CHECKSUM` |
| `haystack_enns_filtered` | metadata filter + dense L2 + early abandon | `FILTERED_OUT`, `DISTANCE_FULL`, `DISTANCE_ABANDONED` |
| `haystack_bm25` | posting-list BM25Okapi-style Q8 scoring | `ACTIVE_DOCS`, `FILTERED_OUT`, `EMPTY_TERMS` |
| `haystack_hybrid_merge` | dense/sparse weighted Q8 merge | `MERGE_MODE`, `DUPLICATES`, `FILTERED`, `OVERFLOW` |
| `haystack_context_pack` | sort + source/chunk dedup + token budget packing | `PACKED_DOC_IDS`, `USED_TOKENS`, skip/truncation counters |
| `haystack_lexrank` | similarity graph + Q8 rank iteration + redundancy filtering | `SELECTED_SENTENCES`, `EDGES`, `ITERATIONS` |

## 行为边界

- 本项目是 micro-benchmark，不是 Haystack、FAISS、rank_bm25、Pyserini、
  NetworkX、sumy 或 LexRank 的移植版。
- `reference/` 给出每个 kernel 的来源、伪代码、瓶颈分析和行为约束。
- C 文件头必须声明 reference archive、benchmark-only extensions、
  simplifications 和 not implemented。
- 数据结构只服务于过程式算法阶段，不模拟 `Retriever`、`DocumentStore`、
  `Pipeline`、FAISS index 或 NetworkX graph 对象。
- Q8 fixed-point、token budget、tie-break、dangling redistribution 等契约不能在
  C 中自行改变；需要改变时必须先更新 reference。
- CGRA 版本受硬件限制，必须遵守 `cgra_flatten_rewrite_plan.md`：单文件单函数、
  无 helper call、无 `main`、无 print，输出通过 buffer 回写；完整算法超限时只能
  摘取或拆分已文档化的 reference slice。

## 维护流程

修改任何 C 行为前，先更新：

1. `reference/<kernel>/source_excerpt.md`
2. `reference/<kernel>/analysis.md`
3. `reference/<kernel>/analysis_zh.md`
4. `ref/kernel_reference_mapping.md`
5. 对应 `src/<kernel>.c` 文件头和实现
6. `tests/check_outputs.sh` 中的行为检查

完成修改后运行：

```bash
make clean
make test
make clean
```
