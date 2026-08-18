# Agentic AI / RAG CPU-side C Benchmark

本仓库提供一组小规模、确定性、可校验的 C99 benchmark，用于评估
agentic AI / RAG 系统中常见的 CPU-side 计算过程。它面向 CGRA/CPU 协同测试：
CGRA 作为 CPU 的协助单元承接边界明确的测试过程，而不是独立运行完整 RAG 系统。

当前代码不会构建知识库，也不运行完整 Haystack、Agent runtime 或机器人规划系统。
这些完整系统依赖动态对象、外部库、模型、I/O 和异步运行时，无法直接交给当前 CGRA
前端。每个 benchmark 因此先从论文和开源实现中确认一个重要 CPU 计算环节，再用固定
输入和明确规则保留其关键循环、分支、状态更新、访存或评分过程。这样做的目标是建立
可重复的 CPU/CGRA 行为与性能对照，而不是把简化程序描述成完整上游应用。

## 快速使用

```bash
make test
```

`make test` 会执行完整验证：

1. 用 `-std=c99 -Wall -Wextra -Werror` 编译全部 C benchmark；
2. 运行 8 个可执行文件并生成 `build/*.out`；
3. 检查 reference 文档、C 文件头、关键 counter、checksum 和 host 行为约束；
4. 检查 CGRA 文件的单函数形态并运行逐项行为 harness；
5. 反汇编全部 CGRA kernel，检查 150 条指令目标和 call-like instruction。

手动拆分执行时，顺序必须是：

```bash
make all
bash tests/run_all.sh
bash tests/check_outputs.sh
bash tests/check_repo_hygiene.sh
bash tests/check_cgra_shape.sh
bash tests/check_cgra_behavior.sh
bash scripts/count_instructions.sh
```

清理运行生成物：

```bash
make clean
```

## 目录结构

```text
PROJECT_OVERVIEW.md                项目背景、benchmark 选择理由、当前状态
README.md                          使用入口
Makefile                           构建、测试、清理入口
SCHEDULING_ROBOTICS_BACKGROUND_ZH.md
                                   scheduling/robotics 背景说明
SCHEDULING_ROBOTICS_CODE_SUMMARY_ZH.md
                                   scheduling/robotics 代码总结
reference_papers/                  论文来源清单；本地 PDF 默认不纳入 Git

reference/                         参考来源、伪代码、瓶颈分析
ref/kernel_reference_mapping.md    reference 到 C 实现边界的中文索引
include/                           公共配置、Top-K、Q8、checksum helper
src/                               8 个独立 C benchmark
cgra_kernels/                      CGRA 单函数 kernel slice
scripts/                           指令数审查和辅助脚本
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
| `agent_workflow_schedule` | DAG readiness + CPU/GPU list placement + successor release | `SCHEDULE_ORDER`, `RESOURCE`, `FINISH` |
| `robot_motion_collision` | fixed-sample edge validity + obstacle collision early exit | `VALID`, `SAMPLES`, `EARLY_EXIT_EDGES` |

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
- CGRA 版本受硬件限制，必须遵守 `cgra_kernels/README.md` 和
  `ref/kernel_reference_mapping.md`：单文件单函数、150 条反汇编指令 practical target、
  无 helper call、无 `main`、无 print、无 `continue`/`break`，输出通过 buffer 回写；
  完整流程超限时只能缩小固定规模或拆分已文档化的算法阶段，并说明重要性、简化原因、
  保留行为和不能外推的结论。

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
