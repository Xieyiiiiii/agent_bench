# Agent CPU-side Benchmark Project Overview

## 项目目标

本项目实现一组小规模、确定性、可追溯的 C99 micro-benchmarks，用于覆盖
agent/RAG 系统中常见的 CPU-side 工作负载。目标不是移植 Haystack、FAISS、
rank_bm25、Pyserini、NetworkX、sumy 或 LexRank，而是从这些项目中抽取
可解释的算法语义，并重写成便于验证和维护的过程式 C benchmark。

硬件使用场景是 CGRA/CPU 协同测试，而不是把完整 RAG 系统搬到加速器上运行。
真实 RAG 需要知识库构建、文档解析、索引维护、框架调度、LLM 调用和大量动态
对象管理，这些都不适合作为当前 CGRA 芯片上的完整 C 程序。当前阶段先选择
开源项目中可追溯的小场景，抽取其中 CPU 侧有代表性的循环、分支、跳转、访存
和评分路径，形成可灌入芯片或在 CPU 侧对照的小核 benchmark。后续如果进入更
完整的 CPU 协同测试，可以在这些小核基础上扩展输入组织方式和测例规模。

当前仓库已经包含 reference 文档、C 源码、公共头文件、Makefile 和测试脚本。
后续维护必须继续遵循 reference-first：先更新 `reference/` 和
`ref/kernel_reference_mapping.md`，再修改 C 行为和测试。

## 为什么选择这 6 个 benchmark

agent 应用中的 CPU 工作通常不只发生在模型推理阶段。即便 LLM 推理在 GPU 或远端
API 上执行，本地 agent 仍然需要完成检索、过滤、排序、融合、上下文整理和压缩。
这些步骤决定端到端延迟、可扩展性、上下文质量和系统稳定性。

这 6 个 benchmark 覆盖 agent/RAG CPU pipeline 的关键阶段：

| Benchmark | Pipeline 位置 | CPU 工作负载 |
|---|---|---|
| `haystack_enns_flat` | dense retrieval baseline | full vector scan, L2 distance, Top-K |
| `haystack_enns_filtered` | filtered dense retrieval | metadata branch, vector scoring, early cutoff extension |
| `haystack_bm25` | sparse lexical retrieval | posting-list traversal, BM25 scoring, active-doc Top-K |
| `haystack_hybrid_merge` | hybrid retrieval fusion | candidate merge, duplicate detection, weighted score fusion |
| `haystack_context_pack` | prompt context preparation | score sort, source/chunk dedup, token budget packing |
| `haystack_lexrank` | context compression | sentence similarity graph, PageRank-style ranking, redundancy filtering |

这些对象共同覆盖了 agent 常见 CPU bottlenecks：连续向量扫描、不规则 posting-list
访问、branch-heavy filtering、小集合合并、排序/去重、矩阵式相似度计算和迭代式
图排名。

## 这些对象在现代 agent 应用中的地位

下列对象描述的是 agent/RAG pipeline 中真实存在的 CPU-side 工作类型，但 benchmark
不声称运行真实 pipeline。每个 C kernel 都只保留一个可验证的小核：例如向量全扫描、
metadata predicate、posting-list traversal、候选融合、context packing 或
LexRank-style 图排名。对于 CGRA 评估来说，重要的是这些小核能稳定制造目标控制流
和数据访问模式，而不是复现完整应用系统。

### 1. Dense retrieval full scan

许多 agent 需要从 memory、document cache、tool outputs 或 embedding store 中找到
与当前 query 最相关的片段。即使生产系统使用 ANN index，flat scan 仍然是最清晰的
baseline：它定义 dense vector retrieval 的基本计算成本，也便于校验 Top-K 行为。

`haystack_enns_flat` 因此用于刻画“连续读取向量 + L2 累加 + Top-K 更新”的基础成本。

### 2. Filtered dense retrieval

真实 agent 很少只按向量相似度检索。它们通常还会按时间、来源、权限、任务类型、
工具域或用户上下文过滤结果。metadata filter 会改变 CPU 行为：有些文档被跳过，
有些进入距离计算，branch prediction 和 filter selectivity 会直接影响性能。

`haystack_enns_filtered` 用固定 predicate 和可验证 counters 模拟这种混合分支 workload。
early abandon 被明确标为 benchmark-only extension，用于暴露 cutoff 分支成本。

### 3. Sparse lexical retrieval

BM25 仍然是 agent/RAG 中非常重要的 fallback 和 complement。它对关键词、标识符、
错误信息、代码符号、日志片段和精确术语很敏感，能弥补 embedding retrieval 的语义
泛化。

`haystack_bm25` 选择 posting-list traversal 和 BM25Okapi scoring，是因为这代表 sparse
retrieval 的核心 CPU 行为：不规则内存访问、per-posting scoring、filter branch 和
active-doc Top-K。

### 4. Hybrid retrieval merge

现代 RAG 系统经常同时使用 dense 和 sparse retrievers。agent 在读取代码、文档、网页
或历史对话时，也经常需要融合语义召回和关键词召回。这个阶段不是大规模矩阵计算，
而是小候选集上的 duplicate detection、score weighting 和 Top-K。

`haystack_hybrid_merge` 用 Haystack-style weighted merge 表达这个阶段。RRF 只作为背景，
除非未来实现 rank-based reciprocal score，否则不得声称实现 RRF。

### 5. Context packing before prompt rendering

agent 的上下文窗口有限，检索结果不能原样全部塞入 prompt。系统通常会排序、去重、
按 token budget 打包，并在必要时截断。这一步直接影响 prompt 质量和下游回答质量。

`haystack_context_pack` 建模 PromptBuilder-adjacent context preparation，而不是 Jinja
模板渲染。它关注 CPU 上的排序、去重、budget branch 和 compact output construction。

### 6. LexRank-style context compression

当检索片段过多或重复度高时，agent 需要做 extractive compression：选择中心性高、
覆盖信息多且不冗余的句子或片段。LexRank-style 算法代表了这类 CPU 工作：两两相似度、
threshold graph、PageRank-style iteration、冗余过滤。

`haystack_lexrank` 覆盖的是 context compression，而不是 LLM summarization。它能暴露
pairwise similarity 和 iterative graph ranking 的 CPU 成本。

## 设计原则

### Reference-first

每个 benchmark 必须有：

- `reference/<kernel>/source_excerpt.md`
- `reference/<kernel>/analysis.md`
- `reference/<kernel>/analysis_zh.md`
- `ref/kernel_reference_mapping.md` 中的映射条目

C 代码只能从这些 reference 语义重写，不能直接复制上游项目结构。

### 过程式 C，而不是小框架

C 实现采用清晰的过程式调用链：

```text
main
  init_data / init_index / init_query
  reset_state / reset_result
  run_kernel
    small algorithm-named helpers
  checksum_result
  print_result
```

数据结构只服务于算法阶段，不模拟 Haystack component、DocumentStore、Pipeline、
FAISS index class 或 NetworkX graph object。每个 kernel 默认只保留少量主要结构：
`Input`/`Index`、`State`、`Result`、`Counters`。

### 可验证行为

每个 benchmark 输出足够的字段来证明关键路径执行过：

- Top-K IDs / scores 或 selected sentence IDs；
- branch counters；
- checksum；
- deterministic tie-break 行为。

测试检查 reference 文件存在性、关键 counters、invalid IDs、checksum 和每个 kernel
的特殊 correctness constraints。

## 当前实现状态

当前仓库包含完整 v0 实现：

- `include/`: 公共配置、Q8 fixed-point、Top-K、checksum helper；
- `src/`: 6 个独立 C99 benchmark；
- `tests/run_all.sh`: 运行所有 benchmark 并生成 `build/*.out`；
- `tests/check_outputs.sh`: 检查 reference、C 文件头、关键 counter 和行为约束；
- `Makefile`: `make all`、`make test`、`make clean`。

`build/` 是运行生成目录，不应提交或手工维护。完成验证后可用 `make clean` 删除。

## CGRA hardware-driven implementation form

当前新增的 CGRA 约束会改变代码形态：芯片端理论上能容纳 `6 * 6 * 16 = 576`
条指令，但 routing 节点会占用实际容量，因此当前实施目标收紧为每个 CGRA 函数
不超过 150 条反汇编指令。编译器前端暂时不能处理函数调用，也不能处理
`continue`/`break`，加速器侧也不依赖 `printf` 这类外设输出。因此当前
`cgra_kernels/` 不是一般软件工程风格的模块化 C，而是硬件可承载的
single-function kernel slice。

这并不改变 reference 行为的来源。`src/` 继续作为 host reference benchmark，用清晰
helper 函数链解释完整算法流程并支持回归测试；`cgra_kernels/` 则把同一 reference 中
最关键的控制流和算术路径扁平化为单函数。若完整算法超出指令预算，允许摘取核心
workload 或拆成多个单函数文件，但必须在 `ref/kernel_reference_mapping.md` 和
`reference/*/analysis*.md` 中写清 slice boundary。

CGRA 版本仍然需要保留复杂分支/跳转特征。`haystack_enns_filtered`、`haystack_bm25`、
`haystack_hybrid_merge`、`haystack_context_pack` 和 `haystack_lexrank` 的 CGRA slice
应分别保留 filter/early abandon、posting traversal、duplicate merge、budget packing
和 dangling rank iteration 等 control-flow-heavy 路径。

## 推荐维护顺序

1. 修改 reference 和 mapping，锁定行为契约。
2. 修改对应 C 文件头，明确 reference、benchmark-only extension 和 not implemented。
3. 修改 C 过程式函数链，保持 `run_kernel()` 只编排主流程。
4. 修改或补充 `tests/check_outputs.sh`，让关键 counter 和 checksum 继续可验证。
5. 执行 `make clean && make test && make clean`。
