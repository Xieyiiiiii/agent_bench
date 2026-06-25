# CGRA Single-function Kernel Rewrite Plan

## 0. 背景和目标

当前仓库的 `src/*.c` 是 host-side reference benchmark：它们强调可读性、
reference-first 映射、可打印输出、checksum 和测试脚本可验证性。新的 CGRA
硬件环境对代码形态提出了更强约束：

- 芯片当前只能 hold `6 * 6 * 16 = 576` 条指令。
- 编译器前端暂时不能处理函数调用。
- 加速器侧没有常规外设反馈能力，kernel 不应依赖 `printf` 等 I/O。
- 单个 C 文件应只包含一个函数，函数内直接展开核心运算流程。
- 如果完整算法超过指令容量，需要摘取最重要的 workload slice，或手动拆成多个
  单函数文件。
- `nw.c` 是当前可接受代码形态的示例：单文件、单函数、无 helper 调用、无 print，
  通过指针参数读写输入输出。

本计划的目标是把现有 6 个 RAG/agent CPU-side benchmark 增加一套 CGRA
frontend-friendly 版本，同时重写项目文档，使仓库明确区分：

1. host reference benchmark：用于行为解释、调试和对照；
2. CGRA kernel slice：用于满足当前硬件和编译器限制。

## 1. 总体设计决策

### 1.1 保留 `src/`，新增 `cgra_kernels/`

推荐保留当前 `src/*.c`，新增 `cgra_kernels/`：

```text
src/
  haystack_enns_flat.c
  haystack_enns_filtered.c
  haystack_bm25.c
  haystack_hybrid_merge.c
  haystack_context_pack.c
  haystack_lexrank.c

cgra_kernels/
  enns_flat_core.c
  enns_filtered_core.c
  bm25_score_core.c
  hybrid_merge_core.c
  context_pack_core.c
  lexrank_rank_core.c
  ...
```

原因：

- `src/` 继续作为可运行、可打印、可回归测试的 host reference。
- `cgra_kernels/` 专门服务 CGRA 编译器限制。
- 文档可以清晰说明 CGRA 版本是 reference 行为的硬件可承载 slice，而不是完整
  host 程序。
- 当 CGRA 编译器未来支持函数调用时，可以再从 host 版本重新下沉更完整的实现。

### 1.2 Host reference 与 CGRA slice 的关系

后续文档必须同时保留两种视角，不能互相覆盖：

- `src/*.c` 是 host reference benchmark。它可以使用 `main -> init -> reset ->
  run_kernel -> helper -> checksum -> print` 的过程式函数链，用于解释完整算法流程、
  运行回归测试和对照 reference 行为。
- `cgra_kernels/*.c` 是 CGRA kernel slice。它必须把 helper 行为内联成单个函数内的
  显式阶段块，不使用 `main`、helper call、print 或 checksum helper。

因此，`reference/*/analysis*.md` 中原有的 host 函数链仍然保留，但必须新增
CGRA 小节，说明：

- CGRA 文件匹配 reference 伪代码中的哪一段；
- 哪些 host helper 行为被内联；
- 哪些 host-only 行为被删除；
- 输出 buffer 中哪些字段证明关键路径被执行；
- 如果不是完整算法，必须写清 slice boundary，不能声称实现了完整 upstream pipeline。

### 1.3 CGRA 版本的代码子集

`cgra_kernels/*.c` 必须遵守：

- 每个文件只有一个函数定义。
- 文件内不定义 `main()`。
- 文件内不调用任何函数，包括 helper、标准库函数、`printf`、`memcpy`、`malloc`。
- 不包含 `stdio.h`。
- 尽量不使用结构体；优先使用指针参数和一维数组。
- 优先使用 `int`、`short`、`char`，减少 ABI 和编译器前端风险。
- 允许使用 `#define` 常量和简单表达式宏。
- 所有输出通过 `out[]`、`score[]`、`rank[]` 等调用方传入的 buffer 回写。
- 不在 kernel 内构造大规模 synthetic data；输入由 host 或测试 harness 准备。
- 优先避免 64-bit arithmetic 和复杂除法。若必须使用除法或宽乘法，必须通过
  反汇编确认没有生成编译器 runtime helper call。

推荐函数形态：

```c
int kernel_name(int *input_a, int *input_b, int *out, int n)
{
    int i;
    int local_state;

    /* stage 1: local init */
    /* stage 2: primary loop */
    /* stage 3: branch/scoring/update */
    /* stage 4: write output buffer */

    return 0;
}
```

### 1.4 分支和跳转覆盖目标

CGRA 版本不是只追求短代码，还必须保留足够复杂的分支/跳转路径。每个 kernel 的
目标如下：

| CGRA kernel | 分支/跳转目标 | 复杂度定位 |
|---|---|---|
| `enns_flat_core.c` | Top-K 插入位置判断、doc/维度双层循环 | baseline，分支复杂度低 |
| `enns_filtered_core.c` | metadata filter、Top-K boundary、early abandon、full/abandoned path | control-flow-heavy |
| `bm25_score_core.c` | query term loop、empty list、posting filter、accepted posting accumulation、active flag | control-flow-heavy |
| `hybrid_merge_core.c` | dense/sparse source branch、duplicate search、filter、overflow、Top-K update | control-flow-heavy |
| `context_pack_core.c` | score ordering、duplicate skip、budget full append/truncate/skip | control-flow-heavy |
| `lexrank_rank_core.c` | dangling node detection、incoming edge scan、fixed rank iteration | control-flow-heavy |

实现时不能为了压低指令数把所有复杂分支删成单一路径。若某个分支因指令预算被删除，
必须在对应 analysis 和 mapping 中说明该 slice 不再覆盖该分支。

## 2. 目录和构建计划

### 2.1 新增目录

```text
cgra_kernels/
  README.md
  enns_flat_core.c
  enns_filtered_core.c
  bm25_score_core.c
  bm25_topk_core.c              # 仅在 score + topk 超限时新增
  hybrid_merge_core.c
  context_pack_core.c
  lexrank_sim_graph_core.c      # 仅在完整 LexRank 超限时新增
  lexrank_rank_core.c
  lexrank_select_core.c         # 仅在需要拆分时新增

scripts/
  count_instructions.sh

tests/
  check_cgra_shape.sh
```

### 2.2 Makefile 目标

新增目标：

```make
cgra-check:
	bash tests/check_cgra_shape.sh
	bash scripts/count_instructions.sh

cgra-clean:
	rm -rf build/cgra
```

完整验证顺序：

```bash
make clean
make test
make cgra-check
make clean
```

其中 `make test` 继续验证 host reference benchmark；`make cgra-check` 验证 CGRA
代码形态和指令预算。

## 3. 指令数审查计划

### 3.1 指令预算

每个 CGRA 单函数文件的目标预算：

```text
MAX_INSTRUCTIONS = 576
```

如果函数超过 576 条，需要按以下顺序处理：

1. 减少输入规模常量，例如 docs、dim、top_k、candidate_k。
2. 删除非核心 counter。
3. 摘取最重要的算法 slice。
4. 拆成多个单函数文件，每个文件独立映射一个 reference 伪代码阶段。

### 3.2 统计脚本

`scripts/count_instructions.sh` 支持两种模式：

```bash
CGRA_CC=<compiler> CGRA_OBJDUMP=<objdump> bash scripts/count_instructions.sh
```

如果没有专用 CGRA 工具链，则使用 host 工具链做近似：

```bash
CC=${CC:-gcc}
OBJDUMP=${OBJDUMP:-objdump}
```

默认 host-GCC 审查使用 freestanding-style flags：

```text
-ffreestanding -fno-builtin -fno-stack-protector -fno-tree-loop-distribute-patterns
```

原因是当前加速器侧没有 libc/runtime，host GCC 不能把简单循环改写成 `memset`，
也不能插入 `__stack_chk_fail` 一类 runtime hook。目标 CGRA 编译器可通过
`CGRA_CFLAGS` 覆盖这些 flags；覆盖后仍必须保证反汇编中没有 `call`、`bl`、
`jal` 等 call-like 指令。

脚本输出每个文件：

```text
cgra_kernels/enns_filtered_core.c  function=enns_filtered_core  instructions=...
```

注意：host `gcc/objdump` 统计只能作为前端审查近似，不等价于 CGRA 后端最终
映射指令数。文档和脚本输出必须明确这一点。

### 3.3 形态检查

`tests/check_cgra_shape.sh` 检查：

- 每个 `cgra_kernels/*.c` 只有一个函数定义。
- 不出现 `main(`。
- 不出现 `printf`、`puts`、`fprintf`、`malloc`、`free`、`memcpy`。
- 不包含 `stdio.h`。
- 反汇编中不出现明显 `call` 指令。
- 文件包含 `CGRA kernel slice` 注释。
- 文件头说明对应 reference kernel 和 slice boundary。

### 3.4 隐式函数调用风险

“C 源码没有函数调用”不等于“目标代码没有调用”。某些后端可能把以下操作降低成
runtime helper call：

- 64-bit multiply/divide；
- signed division 或 modulo；
- 大结构体拷贝；
- 编译器插入的内存 helper；
- 未被目标 ISA 直接支持的宽整数运算。

CGRA kernel 设计规则：

- 优先使用 32-bit integer arithmetic。
- Q8 算术尽量保持 `int` 范围内的乘法、除法和截断。
- 不使用结构体赋值或大数组局部初始化。
- `count_instructions.sh` 不只统计行数，还必须检查反汇编中是否出现 call-like 指令。
- 最终是否满足“无调用”以 CGRA 编译器和 CGRA 反汇编器输出为准；host `objdump`
  只能作为早期近似。

## 4. Kernel 级改写计划

### 4.1 `enns_flat_core.c`

来源：

- host reference: `src/haystack_enns_flat.c`
- reference: `reference/haystack_enns_flat/`

保留：

- full vector scan
- squared L2 accumulation
- Top-K min-distance update
- doc_id tie-break

删除：

- `main`
- `printf`
- checksum
- `TopKItem`
- `l2_distance()` helper
- `update_topk_min_distance()` helper

建议接口：

```c
int enns_flat_core(short *query, short *db, int *out, int num_docs, int dim)
```

`db` 使用 row-major flat layout：

```text
db[doc * dim + d]
```

输出布局：

```text
out[0..TOP_K-1]          topk doc IDs
out[TOP_K..2*TOP_K-1]   topk distances
out[2*TOP_K]            docs_scanned
```

指令风险：低。预计可保留完整核心逻辑。

### 4.2 `enns_filtered_core.c`

来源：

- host reference: `src/haystack_enns_filtered.c`
- reference: `reference/haystack_enns_filtered/`

保留：

- metadata filter
- squared L2
- Top-K boundary valid check
- early abandon
- full/abandoned path counters

删除：

- `DocMeta` 结构体，改为 `int *meta`
- helper 函数
- print/checksum/main

建议 metadata 编码：

```text
meta[doc * 3 + 0] = year
meta[doc * 3 + 1] = domain
meta[doc * 3 + 2] = flags
```

建议接口：

```c
int enns_filtered_core(short *query, short *db, int *meta, int *out,
                       int num_docs, int dim)
```

输出布局：

```text
out[0..TOP_K-1]          topk doc IDs
out[TOP_K..2*TOP_K-1]   topk distances
out[2*TOP_K]            filtered_out
out[2*TOP_K + 1]        distance_full
out[2*TOP_K + 2]        distance_abandoned
out[2*TOP_K + 3]        invalid_boundary_abandon
```

指令风险：中。该 kernel 是复杂分支代表，应优先保留完整。

### 4.3 `bm25_score_core.c`

来源：

- host reference: `src/haystack_bm25.c`
- reference: `reference/haystack_bm25/`

优先保留：

- query term loop
- posting list traversal
- empty term branch
- metadata filter branch
- Q8 BM25 term score
- score accumulation
- active flag update

优先删除：

- Top-K 收集，可必要时拆到 `bm25_topk_core.c`
- index/query 初始化
- IDF 生成
- checksum/print/main/helper

建议接口：

```c
int bm25_score_core(int *query_terms, int *list_start, int *list_len,
                    int *post_doc, int *post_tf, int *doc_len,
                    int *doc_domain, int *idf_q8,
                    int *score_q8, int *active, int *out)
```

输出布局：

```text
out[0] = accepted_postings
out[1] = filtered_out
out[2] = empty_terms
out[3] = active_docs_after_scoring
```

其中 `active_docs_after_scoring` 必须是确定值：遍历 active flag 后统计 active docs。
如果该统计导致指令超限，允许删除该字段，但必须同步修改本计划、mapping 和测试；
不得使用 `estimate` 或 `touched_docs` 这类模糊语义替代。

如果必须保留 Top-K：

```text
bm25_topk_core.c
  input: score_q8, active
  output: topk ids/scores
```

指令风险：高。BM25 的乘除和 posting traversal 都有成本，优先保留 scoring slice。

### 4.4 `hybrid_merge_core.c`

来源：

- host reference: `src/haystack_hybrid_merge.c`
- reference: `reference/haystack_hybrid_merge/`

保留：

- dense/sparse candidate merge
- cross-source duplicate detection
- metadata filter
- weighted Q8 merge
- Top-K max-score update

删除：

- `Candidate`/`MergedCandidate` 结构体
- helper 函数
- print/checksum/main
- RRF 或 duplicate bonus

建议接口：

```c
int hybrid_merge_core(int *dense_doc, int *dense_score_q8,
                      int *sparse_doc, int *sparse_score_q8,
                      int *doc_domain, int *doc_flags, int *out,
                      int dense_k, int sparse_k)
```

输入契约：由于接口没有 `num_docs`，host harness 必须保证 dense/sparse `doc_id`
均是合法 metadata index。CGRA slice 不把输入合法性检查作为核心负载，否则需要改变
接口并增加额外边界分支。

输出布局：

```text
out[0..TOP_K-1]          topk doc IDs
out[TOP_K..2*TOP_K-1]   topk merged scores
out[2*TOP_K]            duplicates
out[2*TOP_K + 1]        filtered
out[2*TOP_K + 2]        overflow
```

指令风险：中。若超限，减少 `DENSE_K`、`SPARSE_K`、`MERGE_MAX`。

### 4.5 `context_pack_core.c`

来源：

- host reference: `src/haystack_context_pack.c`
- reference: `reference/haystack_context_pack/`

保留：

- score ordering
- source/chunk dedup
- token budget branch
- full append / truncate / skip

可能调整：

- 如果插入排序过长，改成固定小 K selection pass。
- 如果 full sort 超限，只保留 top-score selection + budget packing slice。

建议接口：

```c
int context_pack_core(int *doc_id, int *source_id, int *chunk_id,
                      int *token_len, int *score, int *out, int count)
```

输入契约：函数入口必须将 `count` 收敛到 `0..CONTEXT_K`。当 host 提供更多候选时，
该单函数 slice 只处理固定容量窗口，窗口选择或 overflow 统计属于 host/上游测例责任。

输出布局：

```text
out[0..CONTEXT_K-1]      packed doc IDs, invalid = -1
out[CONTEXT_K]           used_tokens
out[CONTEXT_K + 1]       truncated
out[CONTEXT_K + 2]       skipped_duplicate
out[CONTEXT_K + 3]       skipped_budget
```

指令风险：中高。排序和 dedup 同时保留可能接近上限。

### 4.6 LexRank CGRA 拆分

来源：

- host reference: `src/haystack_lexrank.c`
- reference: `reference/haystack_lexrank/`

完整 LexRank 过长风险最高，不建议强行放进一个函数。推荐拆分：

```text
lexrank_sim_graph_core.c
  sentence_sim_q8 + threshold graph construction

lexrank_rank_core.c
  dangling_sum + fixed Q8 rank iterations

lexrank_select_core.c
  rank-based selection + redundancy skip
```

第一阶段优先实现：

```text
lexrank_rank_core.c
```

因为它最能代表：

- iterative graph ranking
- dangling branch
- incoming rank accumulation
- fixed-point truncation drift

`lexrank_rank_core.c` 只匹配 `source_excerpt.md` 中的 PageRank-style iterative update
block，不匹配完整 LexRank pipeline。它不负责 sentence similarity matrix construction、
threshold graph construction 或 redundancy-based summary selection。若后续新增
`lexrank_sim_graph_core.c` 或 `lexrank_select_core.c`，必须分别在 mapping 中说明对应的
reference pseudocode block。

建议接口：

```c
int lexrank_rank_core(int *graph, int *out_degree,
                      int *rank_old_q8, int *rank_new_q8, int *out)
```

输出布局：

```text
rank_old_q8[]            updated in place
out[0]                   iterations
out[1]                   dangling_sources_seen
```

指令风险：高。必须以拆分和摘取为默认策略。

## 5. 文档重写计划

### 5.1 `README.md`

新增内容：

- 仓库包含两类 C：
  - `src/`: host reference benchmark；
  - `cgra_kernels/`: CGRA single-function kernel slices。
- 硬件约束：
  - 576 instruction budget；
  - no function call frontend；
  - no print/I/O；
  - output buffer only。
- 新增使用命令：

```bash
make test
make cgra-check
```

### 5.2 `PROJECT_OVERVIEW.md`

新增章节：

```text
## CGRA hardware-driven implementation form
```

必须说明：

- CGRA 代码形态由硬件和编译器限制决定，不是一般软件工程风格。
- host reference 负责可读性、行为解释和回归验证。
- CGRA kernel slice 负责短路径、单函数、无调用、无 I/O。
- 指令超限时允许摘取核心 workload，但必须文档化 slice boundary。

### 5.3 `agent_cpu_c_bench_initial_plan.md`

新增或重写：

- 原“过程式 helper 函数链”适用于 host reference。
- CGRA 版本改为“单函数显式阶段块”。
- 指令预算和拆分规则。
- `nw.c` 作为代码形态示例。
- `cgra_kernels/`、`scripts/count_instructions.sh`、`tests/check_cgra_shape.sh`
  纳入强制目录结构。

### 5.4 `reference/README.md`

补充：

- reference 行为不要求 CGRA 版本完整实现全部 host 流程。
- CGRA slice 可以摘取 reference 的核心阶段。
- 硬件导致的删减必须写成 `CGRA slice boundary`。
- host analysis 中的 helper 函数链不适用于 CGRA 文件；CGRA analysis 必须说明这些
  helper 如何在单函数内以阶段块形式展开。

### 5.5 `ref/kernel_reference_mapping.md`

每个 kernel 增加：

```text
CGRA kernel form:
CGRA slice boundary:
Instruction budget risk:
Split policy:
Branch/jump coverage:
```

如果某个 kernel 拆分，则增加：

```text
CGRA part mapping:
- cgra_kernels/<file>.c -> reference pseudocode block
```

### 5.6 每个 `reference/<kernel>/analysis_zh.md`

新增：

```text
## CGRA 单函数实现边界
```

说明：

- CGRA 版本保留哪些核心行为；
- 删除哪些 host-only 行为；
- 是否可能拆分；
- 指令数风险；
- 输出 buffer 证明哪些路径执行过。
- 该 CGRA slice 的分支/跳转复杂度目标。
- host helper 函数链与 CGRA 单函数阶段块之间的对应关系。

英文 `analysis.md` 可同步新增：

```text
## CGRA single-function boundary
```

## 6. 测试和验收标准

### 6.1 Host reference 验收

继续要求：

```bash
make clean
make test
```

通过标准：

- 6 个 host benchmark 编译通过；
- `run_all: ok`；
- `check_outputs: ok`。

### 6.2 CGRA shape 验收

新增：

```bash
make cgra-check
```

通过标准：

- 每个 `cgra_kernels/*.c` 只有一个函数定义。
- 没有 `main()`。
- 没有 `printf` 或标准库 I/O。
- 没有 helper 函数调用。
- 反汇编中没有明显 call 指令。
- 每个文件有 CGRA slice 注释。
- 指令数不超过 576，或文件被标记为需要拆分并在本轮修正。
- 对标记为 control-flow-heavy 的 kernel，输出 buffer 必须包含能证明关键分支执行的
  counter 或状态字段。
- 若使用 host 工具链统计，报告必须明确这是近似值；最终验收需要 CGRA 工具链复核。

### 6.3 文档验收

检查：

- README 明确 host reference 与 CGRA kernel slice 的区别。
- Overview 明确硬件约束决定代码形态。
- Plan 明确单函数、无调用、无 I/O、576 指令预算。
- Mapping 每个 kernel 有 CGRA form/slice/risk。
- 每个 analysis_zh 有 `CGRA 单函数实现边界`。

## 7. 实施顺序

1. 先修订 reference 和计划类文档：
   - `reference/README.md`
   - `ref/kernel_reference_mapping.md`
   - `reference/*/analysis.md`
   - `reference/*/analysis_zh.md`
   - `README.md`
   - `PROJECT_OVERVIEW.md`
   - `agent_cpu_c_bench_initial_plan.md`
2. 在每个 analysis 文档中新增 CGRA boundary，消除 host helper 函数链与 CGRA 单函数
   约束之间的歧义。
3. 建立 `cgra_kernels/`、`scripts/`，新增 README 和检查脚本框架。
4. 实现低风险 kernel：
   - `enns_flat_core.c`
   - `enns_filtered_core.c`
5. 实现中风险 kernel：
   - `hybrid_merge_core.c`
   - `context_pack_core.c`
6. 实现高风险 kernel：
   - `bm25_score_core.c`
   - 必要时 `bm25_topk_core.c`
   - `lexrank_rank_core.c`
   - 必要时 `lexrank_sim_graph_core.c` / `lexrank_select_core.c`
7. 跑 `make cgra-check`，查看指令数和 call-like 指令。
8. 对超过 576 条或产生隐式 call 的函数做摘取、改写或拆分。
9. 根据最终拆分结果回填 mapping 和 analysis 中的 slice boundary。
10. 跑完整验证：

```bash
make clean
make test
make cgra-check
make clean
```

11. 检查没有 `build/`、`*.out`、临时汇编或 object 文件残留。

## 8. 风险和决策点

### 8.1 是否保留 host reference

推荐保留。删除 host reference 会降低行为可解释性和回归测试能力。

### 8.2 CGRA 指令统计是否准确

如果没有专用 CGRA 编译器和反汇编器，host 工具链统计只能作为近似。最终需要用
CGRA 工具链复核。

### 8.3 是否允许多个 CGRA 文件对应一个 reference kernel

建议允许。硬件容量限制下，LexRank 和 BM25 很可能无法完整塞进一个函数。只要每个
文件仍是单函数、无调用，并且 mapping 写清楚 slice boundary，就比强行压成一个不可读
或超限函数更可靠。

### 8.4 输出如何验证

CGRA kernel 不 print。验证依赖 host harness 或测试代码读取输出 buffer。当前计划先
在 C 文件中定义输出 buffer layout，后续如需运行级验证，再新增单独 host harness；
host harness 不属于 `cgra_kernels/`，不受单函数限制。

## 9. 完成定义

本轮重写完成后，仓库应满足：

- host reference benchmark 仍可通过 `make test`。
- CGRA kernel slice 可通过 `make cgra-check`。
- 每个 CGRA 文件单函数、无 print、无 helper call。
- 指令数审查结果小于等于 576，或已通过拆分解决。
- 文档明确说明硬件环境如何决定代码流程和代码形式。
- `nw.c` 的示例地位被文档化，不再只是未解释的根目录文件。
