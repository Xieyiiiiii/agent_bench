# Agentic AI / RAG CPU-side C Benchmark v0 Reference-first Plan

## 0. 核心结论

v0 的目标不是移植 Haystack、FAISS、Pyserini、NetworkX、sumy、LexRank
或 rank_bm25，而是从这些开源项目中抽取明确的 CPU-side workload
语义，重写成小规模、确定性、可校验的 C99 benchmark。

硬件目标是 CGRA/CPU 协同 benchmark，不是让当前 CGRA 芯片运行完整 RAG 系统。
真实 RAG 的知识库构建、文档解析、索引维护、框架调度、LLM 调用和动态对象管理
仍然属于 CPU/应用侧。当前 C 程序只承接开源小场景中可追溯的热点小核，用
deterministic synthetic data 近似复杂分支、跳转、访存、partial accumulation、
score fusion 和 Top-K 等 CPU-side 行为。加速器是 CPU 的协助单元，不是完整替代。

后续任何代码生成、审计或重写都必须遵守六条原则：

1. **reference-first**：写 C 前必须先在 `reference/` 中保存参考片段和分析。
2. **可追溯**：每个 kernel 必须能追溯到 repo、源码文件、函数/类方法、URL、抽取语义和简化点。
3. **不冒充等价移植**：C 代码是 micro-benchmark，不是 Python/C++/Java 源码逐行翻译。
4. **可维护**：数据结构按语义聚合，避免松散全局数组、长参数列表、重复状态和过度抽象。
5. **可验证**：synthetic data 必须稳定触发关键分支，输出 counters 和 checksum。
6. **过程清晰**：C 实现采用面向过程风格，函数调用链必须表达算法阶段，
   不用过度拆分的数据结构掩盖流程。

当前 v0 已经包含 `include/`、`src/`、`tests/`、`Makefile` 和根目录
`README.md`。本文件仍作为后续维护和审计的计划/约束文档使用：当 C 行为需要
调整时，必须先回到 reference 和本计划确认边界，再修改实现。

硬件约束更新：当前 CGRA 编译器前端暂时不能处理函数调用，芯片端单个 kernel 还受
`6 * 6 * 16 = 576` 条指令容量约束。因此本文件中关于 `main -> init -> reset ->
run_kernel -> helper -> checksum -> print` 的函数链要求，只适用于 `src/` 下的
host reference benchmark。后续新增的 `cgra_kernels/` 版本必须遵守
`cgra_flatten_rewrite_plan.md`：单文件单函数、无 helper call、无 `main`、无 print，
通过输出 buffer 回写结果。若完整算法超出指令预算，必须摘取 reference 中最重要的
workload slice 或拆成多个单函数文件，并在 mapping 和 analysis 中写清边界。

---

## 1. 强制目录结构

```text
agent_benchmark/
  PROJECT_OVERVIEW.md
  agent_cpu_c_bench_initial_plan.md
  README.md
  Makefile

  reference/
    README.md
    haystack_enns_flat/
      source_excerpt.md
      analysis.md
      analysis_zh.md
    haystack_enns_filtered/
      source_excerpt.md
      analysis.md
      analysis_zh.md
    haystack_bm25/
      source_excerpt.md
      analysis.md
      analysis_zh.md
    haystack_hybrid_merge/
      source_excerpt.md
      analysis.md
      analysis_zh.md
    haystack_context_pack/
      source_excerpt.md
      analysis.md
      analysis_zh.md
    haystack_lexrank/
      source_excerpt.md
      analysis.md
      analysis_zh.md

  ref/
    kernel_reference_mapping.md

  include/
    bench_config.h
    bench_common.h
    rng.h
    checksum.h
    topk.h
    fixed_point.h

  src/
    haystack_enns_flat.c
    haystack_enns_filtered.c
    haystack_bm25.c
    haystack_hybrid_merge.c
    haystack_context_pack.c
    haystack_lexrank.c

  cgra_kernels/
    README.md
    enns_flat_core.c
    enns_filtered_core.c
    bm25_score_core.c
    hybrid_merge_core.c
    context_pack_core.c
    lexrank_rank_core.c
    ...

  scripts/
    count_instructions.sh

  tests/
    run_all.sh
    check_outputs.sh
    check_cgra_shape.sh
```

`PROJECT_OVERVIEW.md` 解释 benchmark 选择依据和这些 CPU workload 在 agent/RAG
应用中的地位。`reference/` 是设计依据目录。`ref/kernel_reference_mapping.md`
是面向 C 文件头和审计的映射索引。三者都必须维护，不能只维护其中一个。

`build/` 是 `make all` 或 `make test` 生成的运行目录，包含可执行文件和
`*.out` 输出，不属于源文件或 reference 文档，完成验证后应通过 `make clean` 清理。

`cgra_kernels/` 是硬件受限版本目录。它不替代 `src/` 的 host reference，而是把
reference 行为中最关键的控制流和算术 slice 扁平化为 CGRA 编译器当前能处理的单函数
C 文件。

---

## 2. Reference-first 工作流

### 2.1 写 C 前必须完成

每个 kernel 在写入或重写 C 之前，必须先更新：

```text
reference/<kernel>/source_excerpt.md
reference/<kernel>/analysis.md
reference/<kernel>/analysis_zh.md
ref/kernel_reference_mapping.md
```

`source_excerpt.md` 必须包含：

```text
Upstream sources:
- repo
- source file or official docs URL
- function/class/method
- why this source is relevant

Core algorithm passage, reduced to pseudocode:
- minimal loop / branch / score / top-k semantics

Benchmark-only extensions:
- any logic not present in upstream, such as early abandon or synthetic filters

C function mapping contract:
- reference pseudocode action -> C function

Behavior matching constraints:
- tie-break, counters, invalid ID rules, checksum coverage
```

`analysis.md` 必须包含：

```text
Why this passage was extracted
C implementation target
What is preserved
What is intentionally omitted
CPU bottleneck modeled
Known correctness constraints
Procedural implementation shape
Behavior matching checks
```

`analysis_zh.md` 必须是对应分析的中文版，至少包含：

```text
为什么抽取这段参考行为
C 实现目标
CPU 瓶颈分析
面向过程实现形态
行为匹配检查
```

### 2.2 不能做的事

- 不把整份第三方源码 vendored 到仓库；
- 不复制大段外部源码作为 C 实现依据；
- 不把 benchmark-only 逻辑写成上游框架行为；
- 不用 repo 首页替代具体源码文件或官方文档 URL；
- 不声称 Haystack、FAISS、Pyserini、NetworkX、sumy、LexRank 或 rank_bm25 在构建或运行时被依赖。

---

## 3. Reference mapping 文件

`ref/kernel_reference_mapping.md` 中每个 kernel 必须包含：

```text
Kernel:
Reference archive:
Reference repo:
Reference file:
Reference function/class/method:
Reference URL:
Referenced code semantics:
C benchmark behavior:
C simplifications:
Benchmark-only extensions:
Not implemented:
Validation requirement:
```

如果一个 C workload 的语义来自多个项目，必须拆开说明。例如：

- Haystack 只提供 retriever API / filter / pipeline shape；
- FAISS 提供 exhaustive L2 flat search；
- rank_bm25 提供 BM25Okapi scoring；
- NetworkX 提供 PageRank-style iterative update；
- benchmark 自己定义 deterministic synthetic data、fixed-point arithmetic、truncation policy 或 early abandon。

`ref/kernel_reference_mapping.md` 应以中文为主，保留必要英文函数名、路径和
upstream project 名称。它是给 code agent 和审计者看的实现边界索引，而不是
上游项目介绍。

---

## 4. C 文件头注释

每个 `src/*.c` 文件开头必须包含：

```c
/*
 * Benchmark:
 *   haystack_xxx
 *
 * Reference archive:
 *   reference/haystack_xxx/source_excerpt.md
 *   reference/haystack_xxx/analysis.md
 *   reference/haystack_xxx/analysis_zh.md
 *
 * Open-source references:
 *   1. Repo:
 *      File:
 *      Function / class / method:
 *      URL:
 *      Referenced semantics:
 *
 * C benchmark behavior:
 *   - ...
 *
 * Benchmark-only extensions:
 *   - ...
 *
 * Simplifications:
 *   - ...
 *
 * Not implemented:
 *   - ...
 *
 * This file reimplements a small deterministic C benchmark from the
 * referenced workload semantics. It does not copy framework structure and
 * does not depend on the reference project at build or run time.
 */
```

必须明确写出不等价点，例如：

- Haystack `Document` objects are replaced by fixed arrays.
- Python float scores are replaced by fixed-point integer scores.
- FAISS heap utilities are replaced by deterministic insertion-based Top-K.
- PromptBuilder template rendering is not implemented.
- Real text tokenizer is replaced by deterministic term IDs.
- Early abandon is a benchmark extension and not Haystack/FAISS equivalence.

---

## 5. v0 kernel 范围

| Kernel | Reference archive | 目标 workload |
|---|---|---|
| `haystack_enns_flat.c` | `reference/haystack_enns_flat/` | dense retrieval full scan + L2 Top-K |
| `haystack_enns_filtered.c` | `reference/haystack_enns_filtered/` | metadata filter + dense L2 scan + branch stress |
| `haystack_bm25.c` | `reference/haystack_bm25/` | posting-list BM25Okapi scoring |
| `haystack_hybrid_merge.c` | `reference/haystack_hybrid_merge/` | dense/sparse candidate merge |
| `haystack_context_pack.c` | `reference/haystack_context_pack/` | context preparation before prompt rendering |
| `haystack_lexrank.c` | `reference/haystack_lexrank/` | LexRank-style extractive compression |

v0 不做：

- 不运行真实 Haystack pipeline；
- 不实现完整 DocumentStore、tokenizer、embedding model、PromptBuilder template rendering；
- 不实现完整 FAISS, Pyserini, NetworkX, sumy, LexRank 或 rank_bm25；
- 不依赖外部输入文件、网络、LLM API 或 GPU API；
- 不声明与参考项目逐行等价。

---

## 6. Kernel-specific 重写要求

### 6.1 `haystack_enns_flat.c`

**目标语义**：给定 query embedding 和 document embeddings，遍历全部
documents，计算 L2 distance，返回 Top-K。

**来源边界**：

- Haystack 只提供 `query_embedding`、`top_k`、filters 和 in-memory retrieval flow；
- FAISS `IndexFlatL2` 提供 exhaustive L2 search 语义；
- deterministic insertion Top-K 是 C benchmark 简化，不是 FAISS heap 移植。

**推荐结构**：

```c
typedef struct {
    int16_t db[NUM_DOCS][DIM];
    int16_t query[DIM];
} EnnsFlatInput;

typedef struct {
    TopKItem topk[TOP_K];
    uint32_t checksum;
} EnnsFlatResult;

typedef struct {
    int32_t docs_scanned;
} EnnsFlatCounters;
```

**实现要求**：

- 使用 deterministic synthetic vectors；
- 使用 squared L2 distance；
- Top-K 使用 deterministic insertion；
- 输出 `TOPK_IDS`、`TOPK_SCORES`、`CHECKSUM`；
- 文件头必须说明 L2 语义来自 FAISS，不是 Haystack 默认 similarity 的完整复刻。

**推荐函数链**：

```text
run_kernel()
  for each doc:
    dist = l2_distance(query, db[doc])
    update_topk_min_distance(topk, doc, dist)
```

Required helper functions: `init_data`, `reset_result`, `l2_distance`,
`run_kernel`, `checksum_result`, `print_result`.

**重写注意事项**：

- C 实现必须能从 `reference/haystack_enns_flat/source_excerpt.md` 的
  exhaustive scan 伪代码直接映射到函数链；
- mapping 和文件头必须包含 `reference/haystack_enns_flat/` 路径和来源边界。

---

### 6.2 `haystack_enns_filtered.c`

**目标语义**：dense retrieval + metadata filter + branch-heavy distance computation。

**来源边界**：

- Haystack 提供 filters 传递和 document filtering 语义；
- FAISS 提供 exhaustive L2 search 语义；
- early abandon 是 benchmark-only extension。

**推荐结构**：

```c
typedef struct {
    uint16_t year;
    uint8_t domain;
    uint8_t flags;
} DocMeta;

typedef struct {
    int16_t db[NUM_DOCS][DIM];
    int16_t query[DIM];
    DocMeta meta[NUM_DOCS];
} EnnsFilteredInput;

typedef struct {
    TopKItem topk[TOP_K];
    uint32_t checksum;
} EnnsFilteredResult;

typedef struct {
    int32_t filtered_out;
    int32_t distance_full;
    int32_t distance_abandoned;
} EnnsFilteredCounters;
```

**实现要求**：

- filter rule 必须在代码注释中写清楚；
- early abandon 只能在 Top-K boundary 已有效时启用；
- early abandon 是 benchmark 扩展，不是 Haystack/FAISS 等价逻辑；
- synthetic data 必须保证 `FILTERED_OUT`、`DISTANCE_FULL`、`DISTANCE_ABANDONED` 均大于 0；
- Top-K 不得全为 invalid。

**推荐函数链**：

```text
run_kernel()
  for each doc:
    if !passes_filter(meta[doc]):
      filtered_out++
      continue
    if topk_boundary_is_valid(topk):
      dist = l2_distance_until_cutoff(query, db[doc], topk_boundary)
    else:
      dist = l2_distance(query, db[doc])
    if distance was complete:
      update_topk_min_distance(topk, doc, dist)
```

Required helper functions: `passes_filter`, `topk_boundary_is_valid`,
`l2_distance`, `l2_distance_until_cutoff`, `run_kernel`, `checksum_result`,
`print_result`.

**重写注意事项**：

- 必须实现 early abandon 的边界条件：只有 `topk[TOP_K - 1].doc_id >= 0`
  时，`topk[TOP_K - 1].score` 才能作为有效 cutoff；
- 文件头需要新增 benchmark-only extension 段落。

---

### 6.3 `haystack_bm25.c`

**目标语义**：给定 query terms、posting lists、doc length 和 IDF，累积分数并返回 Top-K。

**来源边界**：

- Haystack 提供 query/filter/top_k retriever flow；
- `rank_bm25` 的 `BM25Okapi.get_scores()` 提供 v0 scoring target；
- `bm25s` 只提供 sparse/posting-list layout inspiration；
- v0 不实现 Haystack BM25L 完整行为。
- v0 不实现 `BM25Okapi._calc_idf()`、negative-IDF epsilon floor、tokenizer 或 corpus preprocessing；
- `idf_q8[]` 是 `init_index()` 生成的 deterministic synthetic input。

**v0 scoring target**：

```text
score(d, q) += idf(t) * (tf * (k1 + 1)) /
               (tf + k1 * (1 - b + b * doc_len / avg_doc_len))
```

**Q8 fixed-point contract**：

```text
Q8_ONE = 256
q8_mul(a_q8, b_q8) = (a_q8 * b_q8) / Q8_ONE
q8_div(numer_q8, denom_q8) = (numer_q8 * Q8_ONE) / denom_q8

norm_q8 = Q8_ONE - b_q8 + (b_q8 * doc_len[doc_id]) / avg_doc_len
denom_q8 = tf * Q8_ONE + q8_mul(k1_q8, norm_q8)
tf_num_q8 = tf * (k1_q8 + Q8_ONE)
tf_weight_q8 = q8_div(tf_num_q8, denom_q8)
term_score_q8 = q8_mul(idf_q8[t], tf_weight_q8)
score_q8[doc_id] += term_score_q8
```

host reference 版本的所有乘法和除法使用 int64 intermediate。除法 toward zero truncation。
如果 `avg_doc_len == 0` 或 `denom_q8 == 0`，该 term contribution 必须为 0。

CGRA single-function slice 必须尽量保持上述 Q8 运算顺序和 toward-zero truncation，
但如果目标后端会把 64-bit 乘除法降成 runtime helper call，可以在受控输入范围内使用
bounded 32-bit arithmetic。任何这种收窄都必须写入 CGRA slice boundary，并通过反汇编
确认没有 call-like 指令；zero-denominator 行为和 branch/counter 语义不得静默改变。

**推荐结构**：

```c
typedef struct {
    int32_t doc_id;
    uint16_t tf;
} Posting;

typedef struct {
    int32_t start;
    int32_t len;
} PostingList;

typedef struct {
    uint16_t doc_len[NUM_DOCS];
    uint8_t doc_domain[NUM_DOCS];
    int32_t idf_q8[VOCAB_SIZE];
    PostingList lists[VOCAB_SIZE];
    Posting postings[MAX_POSTINGS];
} Bm25Index;

typedef struct {
    uint16_t terms[NUM_QUERY_TERMS];
} Bm25Query;

typedef struct {
    int32_t score_q8[NUM_DOCS];
    uint8_t active[NUM_DOCS];
    TopKItem topk[TOP_K];
} Bm25State;

typedef struct {
    int32_t active_docs;
    int32_t filtered_out;
    int32_t empty_terms;
} Bm25Counters;
```

**实现要求**：

- 不实现 tokenizer，query 使用 deterministic `query_terms[]`；
- 不在 scoring loop 中计算 IDF；
- `idf_q8[]` 必须视为 deterministic input；
- `score_q8[]` 只保存 Q8 分数，不混入 raw integer score；
- fixed-point 变量必须带 `_q8` 后缀；
- `ACTIVE_DOCS` 是输出 counter，不是输入；
- synthetic postings 必须同时产生 filtered docs、active docs、empty terms 和有效 Top-K；
- 文件头必须明确：Haystack 不是 BM25Okapi 公式来源，BM25Okapi 公式来自 `rank_bm25`。

**推荐函数链**：

```text
run_kernel()
  for each query term:
    list = posting_list(term)
    if list is empty: empty_terms++; continue
    score_posting_list_q8(list)
  collect_active_docs_into_topk()
```

Required helper functions: `init_index`, `init_query`, `passes_filter`,
`bm25_term_score_q8`, `score_posting_list_q8`, `collect_active_docs_into_topk`,
`checksum_result`, `print_result`.

**重写注意事项**：

- C scoring 必须严格使用上面的 BM25Okapi target；
- C fixed-point scoring 必须严格使用 Q8 fixed-point contract，不能自行改变
  乘除顺序、舍入策略或 zero-denominator 行为；
- mapping 和文件头必须消除“Haystack BM25L 等同 BM25Okapi”的歧义；
- `avg_doc_len` 是 synthetic constant，必须写入注释；如果未来改为 deterministic
  corpus 统计，必须先更新 reference；
- `BM25Okapi._calc_idf()` 不属于 v0 小核，不能在 C 中临时补实现。

---

### 6.4 `haystack_hybrid_merge.c`

**目标语义**：dense retriever 和 sparse retriever 的结果合并。

**来源边界**：

- Haystack `DocumentJoiner` 提供 join / duplicate / weighted score merge 语义；
- Haystack hybrid retrieval tutorial 提供 dense+sparse pipeline usage；
- Pyserini/RRF 只能作为背景参考，除非 C 代码真的实现 reciprocal rank fusion。

**推荐结构**：

```c
typedef struct {
    int32_t doc_id;
    int32_t score_q8;
} Candidate;

typedef struct {
    Candidate dense[DENSE_K];
    Candidate sparse[SPARSE_K];
    uint8_t doc_domain[NUM_DOCS];
    uint8_t doc_flags[NUM_DOCS];
} HybridInput;

typedef struct {
    int32_t doc_id;
    int32_t dense_score_q8;
    int32_t sparse_score_q8;
    int32_t merged_score_q8;
    uint8_t has_dense;
    uint8_t has_sparse;
} MergedCandidate;

typedef struct {
    MergedCandidate merged[MERGE_MAX];
    int32_t merged_count;
    TopKItem topk[TOP_K];
} HybridState;

typedef struct {
    int32_t duplicates;
    int32_t filtered;
    int32_t overflow;
} HybridCounters;
```

**实现要求**：

- v0 默认实现 Haystack-style weighted score merge；
- duplicate detection、metadata filter、weighted merge 都必须有明确逻辑；
- `dense_weight_q8 + sparse_weight_q8 == Q8_ONE`；
- 缺失 dense 或 sparse 来源时，该来源 weighted component 为 0；
- `score_merged_candidates_q8()` 必须写入 `merged_score_q8`；
- `collect_merged_topk()` 使用 `merged_score_q8` 降序排序，分数相同用更小 `doc_id` tie-break；
- v0 synthetic dense/sparse lists 保证同一来源内部无 duplicate；
- `DUPLICATES` 默认统计跨来源重复，即 sparse candidate 命中已被 dense 接受的 `doc_id`；
- synthetic data 必须触发 `DUPLICATES` 和 `FILTERED`；
- 输出 `DUPLICATES`、`FILTERED`、`OVERFLOW`；
- 不得声称实现 Pyserini RRF，除非 scoring 公式变成 rank-based reciprocal score。

**推荐函数链**：

```text
run_kernel()
  merge_candidate_list(dense, SOURCE_DENSE)
  merge_candidate_list(sparse, SOURCE_SPARSE)
  score_merged_candidates_q8()
  collect_merged_topk()
```

Required helper functions: `passes_filter`, `find_merged`, `insert_or_find_merged`,
`merge_candidate_list`, `score_merged_candidates_q8`, `weighted_merge_score_q8`,
`collect_merged_topk`, `checksum_result`, `print_result`.

**重写注意事项**：

- v0 必须默认实现 pure weighted merge；
- host reference weighted merge 必须使用 Q8 arithmetic 和 int64 intermediate，division toward zero；
- CGRA single-function slice 必须保留 weighted merge、missing source = 0 和 deterministic
  tie-break 语义；如为避免 runtime helper call 使用 bounded 32-bit arithmetic，必须在
  CGRA slice boundary 中说明并通过反汇编验证；
- duplicate bonus 不实现；
- RRF 只能写入 not implemented，不能作为当前 C 行为；
- dense+sparse duplicate bonus 不应默认实现；如果未来保留，必须标成
  benchmark-only extension 并输出单独 counter。

---

### 6.5 `haystack_context_pack.c`

**目标语义**：retrieved documents 进入 LLM prompt 前的 context preparation。

**来源边界**：

- Haystack `PromptBuilder.run()` 主要做变量消费和 template rendering；
- 本 kernel 只能声称模仿 PromptBuilder 消费 documents 前后的 CPU-side
  context preparation workload；
- sort、dedup、token budget packing、truncation 都是 benchmark-defined policy。

**推荐结构**：

```c
typedef struct {
    int32_t doc_id;
    int32_t source_id;
    int32_t chunk_id;
    uint16_t token_len;
    int32_t score;
} ContextCandidate;

typedef struct {
    int32_t doc_id;
    int32_t source_id;
    int32_t chunk_id;
    uint16_t used_tokens;
    uint8_t truncated;
} PackedDoc;

typedef struct {
    ContextCandidate candidates[CONTEXT_K];
} ContextPackInput;

typedef struct {
    PackedDoc packed[CONTEXT_K];
    int32_t packed_count;
    int32_t used_tokens;
    uint32_t checksum;
} ContextPackResult;

typedef struct {
    int32_t truncated;
    int32_t skipped_duplicate;
    int32_t skipped_budget;
} ContextPackCounters;
```

**实现要求**：

- 实现 score sort、source/chunk dedup、token budget packing、optional truncation；
- `TOKEN_BUDGET > 0`；
- `0 < MIN_TRUNC_TOKENS <= TOKEN_BUDGET`；
- full append 时 `PackedDoc.used_tokens = candidate.token_len`，`truncated = 0`；
- truncated append 时 `PackedDoc.used_tokens = remaining_budget`，`truncated = 1`，
  并消耗所有 remaining budget；
- 不实现 Jinja；
- 不实现 string rendering；
- 输出 `PACKED_DOC_IDS`、`USED_TOKENS`、`TRUNCATED`、`SKIPPED_DUPLICATE`、`SKIPPED_BUDGET`。

**推荐函数链**：

```text
run_kernel()
  sort_candidates_by_score()
  for each candidate:
    if is_duplicate_source_chunk(): skipped_duplicate++; continue
    pack_candidate_with_budget()
```

Required helper functions: `sort_candidates_by_score`, `is_duplicate_source_chunk`,
`remaining_budget`, `append_packed_doc`, `pack_candidate_with_budget`,
`checksum_result`, `print_result`.

**重写注意事项**：

- 文件头和 mapping 必须更强地声明这是 PromptBuilder-adjacent workload，
  不是 PromptBuilder 源码移植。
- sort、dedup、budget packing、truncation 必须写成 benchmark-defined policy。

---

### 6.6 `haystack_lexrank.c`

**目标语义**：retrieved snippets 的 LexRank-style extractive summarization / context compression。

**来源边界**：

- `crabcamp/lexrank` 和 `sumy` 提供 sentence similarity graph + centrality + selection 语义；
- NetworkX PageRank 提供 damping/incoming rank propagation 语义；
- fixed iterations、fixed-point rank 和 deterministic sentence-term matrix 是 C benchmark 简化。

**推荐结构**：

```c
typedef struct {
    uint16_t terms[MAX_SENTENCES][MAX_TERMS_SENT];
    uint16_t tf[MAX_SENTENCES][MAX_TERMS_SENT];
    uint8_t len[MAX_SENTENCES];
} SentenceCorpus;

typedef struct {
    uint8_t graph[MAX_SENTENCES][MAX_SENTENCES];
    uint8_t out_degree[MAX_SENTENCES];
    int32_t rank_old_q8[MAX_SENTENCES];
    int32_t rank_new_q8[MAX_SENTENCES];
    int32_t sim_q8[MAX_SENTENCES][MAX_SENTENCES];
} LexRankState;

typedef struct {
    int32_t selected[SUMMARY_K];
    uint32_t checksum;
} LexRankResult;

typedef struct {
    int32_t edges;
    int32_t redundant_skips;
    int32_t iterations;
} LexRankCounters;
```

**实现要求**：

- 如果不实现严格 cosine，函数命名为 `sentence_sim_q8()`；
- `run_rank_iterations()` 必须处理 `out_degree == 0`，不得除以 0；
- v0 默认 dangling policy 是把 dangling rank 均分到所有 destination：

```text
dangling_sum = sum(rank_old[src] for src with out_degree[src] == 0)
incoming = sum(rank_old[src] / out_degree[src] for src -> dst
               where out_degree[src] > 0)
incoming += dangling_sum / N
```

- PageRank/LexRank update 使用：

```text
Q8_ONE = 256
INITIAL_RANK_Q8 = Q8_ONE / N
base_q8 = (Q8_ONE - damping_q8) / N
rank_new_q8[i] = base_q8 + q8_mul(damping_q8, incoming_q8)
```

- fixed iteration 后不做 rank renormalization，Q8 truncation drift 是 benchmark 行为的一部分；
- graph construction 使用 `sim_q8 >= SIM_THRESHOLD_Q8`；
- fixed iteration 可替代 convergence，但必须在注释中说明；
- 不实现 raw text tokenizer，使用 deterministic sentence-term matrix；
- 输出 `SELECTED_SENTENCES`、`EDGES`、`ITERATIONS`、`REDUNDANT_SKIPS`。

**推荐函数链**：

```text
run_kernel()
  compute_similarity_matrix()
  build_threshold_graph()
  run_rank_iterations()
  select_summary_sentences()
```

Required helper functions: `sentence_sim_q8`, `compute_similarity_matrix`,
`build_threshold_graph`, `run_rank_iterations`, `sentence_is_redundant`,
`select_summary_sentences`, `checksum_result`, `print_result`.

**重写注意事项**：

- 推荐把 threshold graph 构造写成显式对称 pair loop，或明确 `EDGES` 是 directed edge count；
- `EDGES` 的统计语义必须写入文件头和 print output 注释；
- dangling redistribution 是 benchmark-defined simplification，不是完整 NetworkX API；
- Q8 rank propagation 必须使用 reference 中的 fixed-point contract；
- 文件头需要添加 `reference/haystack_lexrank/` 路径和 fixed-iteration simplification。

---

## 7. 公共实现规范

### 7.1 面向过程实现风格

本节默认描述 `src/` 下的 host reference benchmark。host 版本追求可读性、可回归测试
和完整算法流程解释，因此允许使用少量 helper 函数。`cgra_kernels/` 下的硬件版本不适用
这些 helper 函数要求；它必须把同样的算法阶段写成单函数内的显式代码块。

每个 kernel 必须用清晰的过程式函数链描述算法阶段。推荐调用链：

```text
main()
  init_data(&input_or_index)
  reset_state(&state_or_result, &counters)
  run_kernel(&input_or_index, &state_or_result, &counters)
  checksum_result(...)
  print_result(...)
```

`run_kernel()` 必须只表达该 kernel 的主算法流程。复杂步骤用少量 helper
函数拆开，helper 名称必须说明算法动作，而不是说明实现细节。例如：

- `passes_filter(...)`
- `l2_distance(...)`
- `l2_distance_until_cutoff(...)`
- `score_posting_q8(...)`
- `merge_candidate_list(...)`
- `pack_candidate(...)`
- `build_similarity_graph(...)`
- `run_rank_iterations(...)`

禁止把整个算法塞进一个超长函数，也禁止为每个数组或每个标量创建一层单字段结构体。

函数粒度要求：

- `run_kernel()` 只编排阶段，不直接包含所有细节；
- helper 函数只做一类算法动作，例如 filter、score、merge、pack、rank；
- helper 调用深度通常不超过两层，避免把简单 C benchmark 写成框架；
- helper 只能通过参数读写状态，不隐式修改文件级全局变量；
- 宏只用于常量和小型 inline 工具，不用于隐藏主算法流程；
- 同一算法阶段只保留一个权威实现，避免 `score_doc`、`score_candidate`、
  `compute_score` 这类重复入口。

CGRA benchmark 额外约束：

- 不为了模拟真实 RAG 引入知识库、document store、pipeline、tokenizer 或 prompt
  renderer 对象；
- synthetic data 的职责是稳定触发目标分支、跳转、访存和评分模式，不是伪装成真实语料；
- 小核内部可以近似真实 workload 的复杂控制流，但必须在 reference 中标成
  benchmark-defined policy；
- 如果某个近似行为是为了芯片测试而加入的，必须有注释、counter 或输出字段证明其执行。

CGRA single-function 版本额外约束：

- 每个 `cgra_kernels/*.c` 文件只能有一个函数定义；
- 不能定义 `main()`；
- 不能调用 helper、标准库函数、`printf`、`memcpy`、`malloc`；
- 不包含 `stdio.h`；
- 尽量使用 flat arrays 和简单标量，不使用冗余 wrapper 结构；
- 输出通过调用方传入的 buffer 回写；
- 反汇编不得出现 call-like 指令；
- 每个文件的指令数目标不超过 576；
- 对 control-flow-heavy kernel，必须保留能代表复杂分支/跳转的路径，并用输出字段证明执行。

### 7.2 数据结构复杂度上限

数据结构服务于过程，不反过来支配过程。每个 kernel 默认只允许以下类别：

- `Input` 或 `Index`：只放稳定输入和 synthetic corpus/index；
- `State`：只放跨阶段中间状态，简单 kernel 可省略；
- `Result`：只放最终输出；
- `Counters`：只放验证和分支覆盖 counters。

复杂 kernel 最多使用 4 个主要上下文结构。简单 kernel 应优先使用
`Input + Result + Counters` 或 `Index + Query + State + Counters`，不得为了
“架构感”拆出大量微型结构。局部、短生命周期变量优先放在函数栈上。

### 7.3 行为匹配要求

每个 kernel 必须能从 `reference/<kernel>/source_excerpt.md` 的伪代码直接
映射到 C 函数调用链。实现者必须在写 C 前回答：

```text
Which function implements each pseudocode block?
Which counters prove the important branches ran?
Which simplifications are benchmark-only?
Which output fields allow deterministic regression checks?
```

如果 C 函数无法对应 reference 伪代码中的阶段，优先调整函数边界，而不是新增复杂
结构体。

### 7.4 类型

公共结构体和 ABI-facing 结构体优先使用固定宽度类型：

```c
int32_t doc_id;
int32_t score;
uint16_t term_id;
uint16_t tf;
uint32_t checksum;
```

避免在 ABI-facing structs 中使用裸 `int`。

### 7.5 数据结构细则

本节补充 7.2。优先使用“结构体聚合 + 内部 fixed-size flat arrays”。

原则：

- 相关数据放在同一个 `Input`、`Index`、`State`、`Result`、`Counters` 结构体中；
- 简单 kernel 使用 `Input + Result + Counters`；
- 复杂 kernel 可增加一个 `State`，但不应超过 4 个主要上下文结构；
- 不把十几个数组作为函数参数传来传去；
- 不把所有数组做成无组织的文件级全局变量；
- 不把每个数组包成一个单字段结构体；
- 不在多个结构体中重复保存同一份权威数据；
- `Counters` 只放统计量；
- `Result` 只放最终输出；
- `State` 只放中间状态，并由 `reset_state()` 或 `run_kernel()` 开头显式初始化；
- helper 函数不得隐式读写文件级全局变量，除非该变量是只读常量表。

面向过程实现中，数据结构命名必须跟算法阶段一致，而不是跟未来可能的框架对象一致。
例如用 `Bm25Index`、`Bm25State`，不要引入 `Retriever`, `DocumentStore`,
`Pipeline`, `Component` 等模拟框架对象。

### 7.6 Determinism

- synthetic data 必须由固定 seed 或闭式公式生成；
- 输出必须稳定；
- checksum 必须覆盖 Top-K/selected IDs、scores 和关键 counters；
- tie-break 必须 deterministic，通常使用更小 `doc_id` 或 `sentence_id`。

### 7.7 Fixed-point

- 使用 Q8 fixed-point 时变量名必须带 `_q8`；
- 不把 fixed-point 值和 raw integer score 混在同一个变量名中；
- 除法 helper 必须处理 denominator 为 0 的情况；
- 文件头必须说明 Python float / NumPy scoring 被替换为 fixed-point integer scoring。

### 7.8 Source excerpt 到 C 函数的映射

每个 `reference/<kernel>/source_excerpt.md` 的伪代码块必须能映射到 C 函数。
实现时在本地检查以下映射：

| Reference pseudocode action | C location requirement |
|---|---|
| initialize input/index | `init_data`, `init_index`, or `init_query` |
| reset outputs/intermediates | `reset_result` or `reset_state` |
| primary loop | `run_kernel` |
| predicate branch | `passes_filter` or domain-specific predicate helper |
| scoring/distance/ranking math | one clearly named helper with `_q8` suffix when fixed-point |
| top-k/selection | `update_topk_*`, `collect_*_topk`, or `select_*` |
| validation fields | `Counters`, `checksum_result`, and `print_result` |

如果某个 reference 伪代码动作无法映射到一个清晰函数，说明函数边界还不够好。

---

## 8. 测试和验证要求

重建 `tests/check_outputs.sh` 时，不能只做 smoke test。它至少必须检查：

- 检查每个 `src/*.c` 文件头包含 `Reference archive:`；
- 检查每个 kernel 的 `reference/<kernel>/source_excerpt.md` 和 `analysis.md` 存在；
- 检查每个 kernel 的 `reference/<kernel>/analysis_zh.md` 存在，并包含
  `CPU 瓶颈分析`、`面向过程实现形态`、`行为匹配检查`；
- `haystack_enns_filtered`: Top-K 未填满前不得 early abandon；
- `haystack_bm25`: `ACTIVE_DOCS > 0`、`FILTERED_OUT > 0`、`EMPTY_TERMS > 0`，
  并检查 `idf_q8[]` 是 deterministic input，不在 scoring loop 中计算；
- `haystack_hybrid_merge`: `DUPLICATES > 0`、`FILTERED > 0`，并验证 duplicate
  语义是跨 dense/sparse 来源重复，scoring 是 pure weighted merge；
- `haystack_context_pack`: 至少一个 skip/truncation counter 非零；
- `haystack_lexrank`: `EDGES > 0`，如果实现对称图则验证对称性，并检查
  `out_degree == 0` 有 dangling redistribution 或等价防除零策略；
- 所有 Top-K / selected IDs 不得全为 invalid；
- 所有 benchmarks 必须输出 checksum。

---

## 9. 推荐后续执行顺序

1. 更新 `reference/` 和 `ref/kernel_reference_mapping.md`。
2. 更新 6 个 C 文件头注释，补充 `Reference archive:` 和 benchmark-only boundaries。
3. 实现 `haystack_enns_filtered.c` 时加入 early abandon cutoff 有效性检查。
4. 实现 `haystack_hybrid_merge.c` 时默认采用 pure weighted merge；RRF 写入 not implemented。
5. 实现 `haystack_lexrank.c` 时明确 graph edge count 语义。
6. 增强 `tests/check_outputs.sh` 的 reference 文件存在性检查和关键 counter 检查。
7. 运行：

```bash
make test
```

或手动运行：

```bash
make all
bash tests/run_all.sh
bash tests/check_outputs.sh
```

`tests/check_outputs.sh` 会读取 `build/*.out`，因此手动执行时必须先运行
`tests/run_all.sh` 生成输出。只有上述步骤全部完成后，才能说 v0 benchmark
在“可追溯、可验证、不冒充移植”的意义上合格。

---

## 10. 实现准入清单

写任何 `src/haystack_*.c` 前，必须逐项确认：

### 10.1 Reference 完整性

- `source_excerpt.md` 有 upstream source URL、核心伪代码、benchmark-only extension；
- `source_excerpt.md` 有 `C function mapping contract`；
- `analysis.md` 和 `analysis_zh.md` 都说明 CPU bottleneck；
- `analysis.md` 和 `analysis_zh.md` 都说明过程式函数链；
- 如果实现 `cgra_kernels/`，`analysis.md` 和 `analysis_zh.md` 都必须包含 CGRA
  single-function boundary；
- `ref/kernel_reference_mapping.md` 同步列出三个 reference archive 文件。

### 10.2 行为匹配

- 每个 reference 伪代码动作都能映射到一个明确 C 函数；
- 对 CGRA 版本，每个 CGRA 文件都必须映射到一个明确 reference slice；
- 每个 benchmark-only extension 都有注释和 counter；
- 每个 CGRA 近似行为都明确写成 benchmark-defined policy，不能冒充上游行为；
- 每个 critical branch 都有 deterministic synthetic data 触发；
- 每个输出字段都能用于 regression check；
- checksum 覆盖 Top-K/selected IDs、scores 和关键 counters。

### 10.3 过程式可读性

- `main()` 只串联 init/reset/run/checksum/print；
- `run_kernel()` 只编排主流程；
- helper 名称体现算法动作，不体现框架对象；
- 不引入 `Retriever`、`DocumentStore`、`Pipeline`、`Component` 等框架模拟结构；
- 不为单个数组或标量创建单字段 wrapper；
- 复杂 kernel 不超过 4 个主要上下文结构。

### 10.4 最小数据模型

| Kernel | 允许的主要结构 | 不应新增 |
|---|---|---|
| `haystack_enns_flat` | `EnnsFlatInput`, `EnnsFlatResult`, `EnnsFlatCounters` | `State`, fake index object |
| `haystack_enns_filtered` | `DocMeta`, `EnnsFilteredInput`, `EnnsFilteredResult`, `EnnsFilteredCounters` | filter AST, filter object hierarchy |
| `haystack_bm25` | `Posting`, `PostingList`, `Bm25Index`, `Bm25Query`, `Bm25State`, `Bm25Counters` | tokenizer, corpus object, document store |
| `haystack_hybrid_merge` | `Candidate`, `HybridInput`, `MergedCandidate`, `HybridState`, `HybridCounters` | hash map, duplicate manager, RRF object |
| `haystack_context_pack` | `ContextCandidate`, `PackedDoc`, `ContextPackInput`, `ContextPackResult`, `ContextPackCounters` | template renderer, token-budget object |
| `haystack_lexrank` | `SentenceCorpus`, `LexRankState`, `LexRankResult`, `LexRankCounters` | graph API wrapper, matrix wrapper objects |

如果实现中需要突破这些限制，必须先更新 reference analysis，说明新增结构本身为什么是
CPU workload 的一部分。
