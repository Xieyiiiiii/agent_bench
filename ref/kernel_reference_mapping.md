# Kernel Reference Mapping

本文件是 `reference/<kernel>/` 与未来 `src/*.c` 文件头之间的中文审计索引。
它不描述完整上游项目移植，而是说明每个 C benchmark 从哪些开源项目抽取小核
语义、哪些行为由 benchmark 自己定义、以及 code agent 写 C 时必须遵守的函数
和数据边界。

硬件场景：当前 CGRA 芯片不运行完整 RAG 系统，也不构建真实知识库。加速器是
CPU 的协助单元，本阶段先选取开源项目中可追溯的小场景，把 CPU 侧常见的循环、
复杂分支、跳转、访存和评分路径重写成确定性 C benchmark。后续 CPU 协同测试
可以在这些小核基础上扩展更完整的测例。

通用约束：

- C 文件必须匹配本文件和 `reference/<kernel>/source_excerpt.md` 中定义的小核行为。
- 不得声称逐行移植 Haystack、FAISS、rank_bm25、Pyserini、NetworkX、sumy 或 LexRank。
- 数据结构只服务于过程式算法阶段，不模拟 `Retriever`、`DocumentStore`、`Pipeline`
  或图 API 对象。
- 每个 kernel 的输出必须包含能证明关键路径执行过的 IDs/scores/counters/checksum。

## haystack_enns_flat.c

Kernel: `haystack_enns_flat.c`

参考归档：
- `reference/haystack_enns_flat/source_excerpt.md`
- `reference/haystack_enns_flat/analysis.md`
- `reference/haystack_enns_flat/analysis_zh.md`

参考来源明细：
- Repo: `deepset-ai/haystack`
  - File: `haystack/components/retrievers/in_memory/embedding_retriever.py`
  - Function / method: `InMemoryEmbeddingRetriever.run()`
  - URL: `https://github.com/deepset-ai/haystack/blob/main/haystack/components/retrievers/in_memory/embedding_retriever.py`
  - 提供语义：query embedding、runtime/configured `top_k`、retriever flow。
- Repo: `deepset-ai/haystack`
  - File: `haystack/document_stores/in_memory/document_store.py`
  - Function / method: embedding retrieval path
  - URL: `https://github.com/deepset-ai/haystack/blob/main/haystack/document_stores/in_memory/document_store.py`
  - 提供语义：in-memory document embedding scoring 和 Top-K retrieval flow。
- Repo: `facebookresearch/faiss`
  - File: `faiss/IndexFlat.cpp`
  - Function / method: `IndexFlat::search()`, `IndexFlatL2`
  - URL: `https://github.com/facebookresearch/faiss/blob/main/faiss/IndexFlat.cpp`
  - 提供语义：exhaustive L2 search、distances、labels。
- Repo: `facebookresearch/faiss`
  - File: `faiss/utils/Heap.h`
  - Function / method: heap utilities
  - URL: `https://github.com/facebookresearch/faiss/blob/main/faiss/utils/Heap.h`
  - 提供语义：Top-K 背景；C benchmark 不移植 heap 实现。

来源边界：
- Haystack `InMemoryEmbeddingRetriever.run()` 和 in-memory document store 只提供
  query embedding、`top_k`、in-memory retrieval flow。
- FAISS `IndexFlatL2` 提供 exhaustive L2 search、distances、labels 的核心语义。
- FAISS heap utilities 只作为 Top-K 背景，不在 C 中移植。
- Benchmark 定义 deterministic int16 vectors、insertion Top-K 和 checksum。

C 行为：
- 遍历所有 documents。
- 对每个 document 计算 squared L2 distance。
- 用 deterministic insertion 维护最小距离 Top-K。
- 距离相同用更小 `doc_id` tie-break。

允许的主要数据结构：
- `EnnsFlatInput`
- `EnnsFlatResult`
- `EnnsFlatCounters`

不得新增：
- `State`
- fake index object
- Haystack/FAISS class-like wrapper

C 函数契约：
- 初始化 synthetic vectors -> `init_data`
- 初始化 Top-K invalid slots -> `reset_result`
- 主 document loop -> `run_kernel`
- squared L2 -> `l2_distance`
- Top-K 插入 -> `update_topk_min_distance`
- 输出稳定校验 -> `checksum_result`, `print_result`

验证要求：
- `DOCS_SCANNED == NUM_DOCS`
- Top-K IDs 有效且非全 invalid。
- Top-K scores 按 L2 distance 升序。
- checksum 覆盖 IDs、scores、`DOCS_SCANNED`。

CGRA kernel form:
- `cgra_kernels/enns_flat_core.c`
- 单文件单函数，无 `main`、无 helper call、无 print。
- host helper `l2_distance` 和 `update_topk_min_distance` 在单函数内展开。

CGRA slice boundary:
- 覆盖 exhaustive L2 scan、Top-K min-distance update、doc_id tie-break。
- 不覆盖 host synthetic data 初始化、checksum、print。

Instruction budget risk:
- 低。该 kernel 是 dense scan baseline，预计可完整保留。

Split policy:
- 默认不拆分。理论硬件上限是 576 条指令，但当前 practical target 是 150 条；
  若未来超过实际阈值，优先减少 `num_docs` 或 `dim`，不删除 Top-K tie-break。

Branch/jump coverage:
- baseline。覆盖 document/维度双层循环和 Top-K 插入位置判断，分支复杂度较低。

## haystack_enns_filtered.c

Kernel: `haystack_enns_filtered.c`

参考归档：
- `reference/haystack_enns_filtered/source_excerpt.md`
- `reference/haystack_enns_filtered/analysis.md`
- `reference/haystack_enns_filtered/analysis_zh.md`

参考来源明细：
- Repo: `deepset-ai/haystack`
  - File: `haystack/components/retrievers/in_memory/embedding_retriever.py`
  - Function / method: `InMemoryEmbeddingRetriever.run()`
  - URL: `https://github.com/deepset-ai/haystack/blob/main/haystack/components/retrievers/in_memory/embedding_retriever.py`
  - 提供语义：query embedding、filters、`top_k` 传入 retrieval flow。
- Repo: `deepset-ai/haystack`
  - File: `haystack/document_stores/in_memory/document_store.py`
  - Function / method: filtering and embedding retrieval path
  - URL: `https://github.com/deepset-ai/haystack/blob/main/haystack/document_stores/in_memory/document_store.py`
  - 提供语义：retrieval 前 metadata filtering。
- Repo: `facebookresearch/faiss`
  - File: `faiss/IndexFlat.cpp`
  - Function / method: `IndexFlat::search()`, `IndexFlatL2`
  - URL: `https://github.com/facebookresearch/faiss/blob/main/faiss/IndexFlat.cpp`
  - 提供语义：exhaustive squared L2 search。

来源边界：
- Haystack 提供 filters 传入 retriever 并在 retrieval 前过滤 documents 的行为边界。
- FAISS `IndexFlatL2` 提供 exhaustive squared L2 search 语义。
- Benchmark 定义固定 metadata predicate、deterministic metadata、early abandon。
- Early abandon 不是 Haystack/FAISS 等价行为，只用于近似 CGRA 测试中的复杂分支和
  partial accumulation。

C 行为：
- 先执行 `passes_filter(meta[d])`。
- 被 filter 拒绝的 document 不进入距离计算。
- Top-K 未填满前必须完整计算 L2。
- 只有 `topk[TOP_K - 1].doc_id >= 0` 后，才允许使用 boundary score 做 cutoff。
- partial distance 超过有效 boundary 时可 abandon。

允许的主要数据结构：
- `DocMeta`
- `EnnsFilteredInput`
- `EnnsFilteredResult`
- `EnnsFilteredCounters`

不得新增：
- filter AST
- filter object hierarchy
- FAISS-like index wrapper

C 函数契约：
- 初始化 vectors 和 metadata -> `init_data`
- 初始化 Top-K/counters -> `reset_result`
- metadata predicate -> `passes_filter`
- boundary 有效性 -> `topk_boundary_is_valid`
- 完整 L2 -> `l2_distance`
- cutoff L2 -> `l2_distance_until_cutoff`
- 主流程编排 -> `run_kernel`
- 输出稳定校验 -> `checksum_result`, `print_result`

验证要求：
- `FILTERED_OUT > 0`
- `DISTANCE_FULL > 0`
- `DISTANCE_ABANDONED > 0`
- Top-K IDs 有效。
- early abandon 不得在 Top-K boundary 无效时发生。

CGRA kernel form:
- `cgra_kernels/enns_filtered_core.c`
- 单文件单函数，无结构体；metadata 使用 flat `int meta[]`。
- 150 指令计划下使用 Top-2 CGRA slice；host reference 仍保留 Top-4 测例语义。
- 行为对比时必须使用 Top-2 slice reference，或只比较 host Top-4 结果中的前两个
  IDs/scores；不得要求 CGRA Top-2 输出匹配完整 Top-4 布局。
- `distance_full`、`distance_abandoned` 等 counters 必须来自 Top-2 reference slice；
  不能直接用 Top-4 reference counters 对比 Top-2 CGRA，因为 boundary valid 时机不同。
- 推荐输出布局为 `out[0..1] = top2 doc IDs`、`out[2..3] = top2 squared L2`
  distances、`out[4] = filtered_out`、`out[5] = distance_full`、
  `out[6] = distance_abandoned`。
- 不使用 `continue`/`break`；filter reject 和 abandon path 用嵌套 `if` 与 `complete`
  flag 表达。
- host helper `passes_filter`、`topk_boundary_is_valid`、`l2_distance_until_cutoff`、
  `update_topk_min_distance` 在单函数内展开。

CGRA slice boundary:
- 覆盖 metadata filter、完整 L2、boundary-valid 判断、early abandon、Top-2 update。
- 不覆盖 host 初始化、checksum、print。
- `invalid_boundary_abandon` 是调试性防御 counter，150 指令 slice 可删除；early abandon
  仍必须只在 boundary valid 时发生。

Instruction budget risk:
- 中。该 kernel 必须优先保留完整复杂控制流；若要压到 150，优先降低 Top-K 宽度，
  不先删除 filter 或 early abandon。

Split policy:
- 默认不拆分；若超过 150 指令，优先使用 Top-2 和精简 counter。仍超限时再缩小
  `num_docs` 或 `dim`，不能首先删除 early abandon 或 boundary-valid 检查。

Branch/jump coverage:
- control-flow-heavy。必须覆盖 filtered path、full-distance path、abandoned path 和
  valid-boundary path。

## haystack_bm25.c

Kernel: `haystack_bm25.c`

参考归档：
- `reference/haystack_bm25/source_excerpt.md`
- `reference/haystack_bm25/analysis.md`
- `reference/haystack_bm25/analysis_zh.md`

参考来源明细：
- Repo: `deepset-ai/haystack`
  - File: `haystack/components/retrievers/in_memory/bm25_retriever.py`
  - Function / method: `InMemoryBM25Retriever.run()`
  - URL: `https://github.com/deepset-ai/haystack/blob/main/haystack/components/retrievers/in_memory/bm25_retriever.py`
  - 提供语义：query/filter/top_k retriever flow。
- Repo: `deepset-ai/haystack`
  - File: `haystack/document_stores/in_memory/document_store.py`
  - Function / method: BM25 retrieval path
  - URL: `https://github.com/deepset-ai/haystack/blob/main/haystack/document_stores/in_memory/document_store.py`
  - 提供语义：in-memory keyword retrieval、filter、Top-K flow。
- Repo: `dorianbrown/rank_bm25`
  - File: `rank_bm25.py`
  - Function / method: `BM25Okapi.get_scores()`
  - URL: `https://github.com/dorianbrown/rank_bm25/blob/master/rank_bm25.py`
  - 提供语义：BM25Okapi term scoring target。
- Repo: `xhluca/bm25s`
  - File: `bm25s/scoring.py`
  - Function / method: sparse scoring routines
  - URL: `https://github.com/xhluca/bm25s/blob/main/bm25s/scoring.py`
  - 提供语义：posting/sparse scoring layout 背景；不作为 C 行为等价契约。

来源边界：
- Haystack `InMemoryBM25Retriever.run()` 和 document store 提供 query/filter/top_k
  retriever flow。
- `rank_bm25` `BM25Okapi.get_scores()` 提供 BM25Okapi term scoring target。
- `bm25s` 只提供 posting-list / sparse layout 背景。
- Benchmark 定义 deterministic query terms、postings、metadata、doc lengths、
  `idf_q8[]` 和 fixed-point arithmetic。

明确不实现：
- tokenizer
- corpus preprocessing
- `BM25Okapi._calc_idf()`
- negative-IDF epsilon flooring
- Haystack BM25L 完整行为
- bm25s CSR internals

C 行为：
- `init_index()` 生成 deterministic postings、doc stats、metadata、`idf_q8[]`。
- `init_query()` 生成 deterministic query term IDs。
- 对每个 query term 遍历 posting list。
- empty list 只增加 `EMPTY_TERMS`。
- filtered posting 不影响 score/active flag。
- accepted posting 调用唯一 scoring helper `bm25_term_score_q8()`。
- Q8 scoring 使用 int64 intermediates、toward-zero truncation 和 zero-denominator
  contribution = 0 的固定契约。
- 最终只从 active documents 收集 Top-K。

允许的主要数据结构：
- `Posting`
- `PostingList`
- `Bm25Index`
- `Bm25Query`
- `Bm25State`
- `Bm25Counters`

不得新增：
- tokenizer
- corpus object
- document store
- idf calculator object
- sparse matrix wrapper

C 函数契约：
- index/postings/doc stats 初始化 -> `init_index`
- query terms 初始化 -> `init_query`
- metadata predicate -> `passes_filter`
- BM25Okapi Q8 term contribution -> `bm25_term_score_q8`
- posting traversal -> `score_posting_list_q8`
- active-doc Top-K -> `collect_active_docs_into_topk`
- 主流程编排 -> `run_kernel`
- 输出稳定校验 -> `checksum_result`, `print_result`

验证要求：
- `ACTIVE_DOCS > 0`
- `FILTERED_OUT > 0`
- `EMPTY_TERMS > 0`
- Top-K IDs 有效。
- fixed-point 变量名使用 `_q8`。
- Q8 乘除顺序、舍入和 denominator 为 0 的行为必须匹配 reference。
- checksum 覆盖 Top-K IDs、scores、active/filter/empty counters。

CGRA kernel form:
- 第一阶段为 `cgra_kernels/bm25_score_core.c`。
- 如 Top-K collection 导致超限，可新增 `cgra_kernels/bm25_topk_core.c`。
- 单文件单函数；posting arrays、doc stats、idf_q8、score_q8、active flags 通过指针传入。

CGRA slice boundary:
- `bm25_score_core.c` 覆盖 query term loop、posting-list traversal、empty term branch、
  metadata filter、Q8 contribution、score accumulation、active flag update。
- 第一阶段不强制覆盖 active-doc Top-K；若拆出 `bm25_topk_core.c`，该文件单独匹配
  active-doc Top-K collection。
- 不覆盖 tokenizer、IDF 计算、index/query 初始化、checksum、print。

Instruction budget risk:
- 高。BM25 的 posting traversal、除法和 fixed-point scoring 在理论 576 条上限内
  仍可能偏大；当前 practical target 是 150 条，必须按 slice 审查。
- host reference 使用 int64 intermediates 定义数学语义；CGRA slice 优先使用受控输入和
  32-bit 算术，必须通过反汇编确认没有 runtime helper call。

Split policy:
- 优先保留 scoring slice；Top-K 可拆分。
- 若除法或宽乘法产生 call-like 指令，必须缩小输入范围、改写算术或进一步摘取核心评分路径。
- 输出 counter 必须使用确定语义：`accepted_postings`、`filtered_out`、`empty_terms`、
  `active_docs_after_scoring`；不得使用 estimate。

Branch/jump coverage:
- control-flow-heavy。必须覆盖 query term loop、empty posting list、filtered posting、
  accepted posting accumulation 和 active flag update。

## haystack_hybrid_merge.c

Kernel: `haystack_hybrid_merge.c`

参考归档：
- `reference/haystack_hybrid_merge/source_excerpt.md`
- `reference/haystack_hybrid_merge/analysis.md`
- `reference/haystack_hybrid_merge/analysis_zh.md`

参考来源明细：
- Repo: `deepset-ai/haystack`
  - File: `haystack/components/joiners/document_joiner.py`
  - Function / method: `DocumentJoiner.run()`, weighted merge mode
  - URL: `https://github.com/deepset-ai/haystack/blob/main/haystack/components/joiners/document_joiner.py`
  - 提供语义：join multiple document lists、duplicate handling、weighted merge。
- Docs: Haystack hybrid retrieval tutorial
  - File / page: tutorial 33 hybrid retrieval
  - Function / method: dense+sparse retriever pipeline usage
  - URL: `https://haystack.deepset.ai/tutorials/33_hybrid_retrieval`
  - 提供语义：dense/sparse outputs joined before downstream RAG。
- Repo: `castorini/pyserini`
  - File: `README.md`
  - Function / method: hybrid / RRF background references
  - URL: `https://github.com/castorini/pyserini/blob/master/README.md`
  - 提供语义：rank fusion 背景；RRF 不作为 v0 C 行为。

来源边界：
- Haystack `DocumentJoiner` 提供 join、duplicate document handling、weighted
  score merge 的组件语义。
- Haystack hybrid retrieval tutorial 提供 dense+sparse retriever output 被 join 的
  pipeline 背景。
- Pyserini/RRF 只作为 rank fusion 背景。
- Benchmark 定义 deterministic dense/sparse candidate lists、metadata filter、
  fixed-size merged table 和 pure weighted merge。

明确不实现：
- full Haystack join modes
- Pyserini search stack
- RRF scoring
- duplicate bonus
- dynamic `Document` object

C 行为：
- dense list 先 merge，sparse list 后 merge。
- v0 synthetic input 保证同一来源内部无 duplicate。
- v0 synthetic input 和 CGRA host harness 必须保证 dense/sparse `doc_id` 是合法 metadata
  index；CGRA slice 没有 `num_docs` 参数，不在加速器侧做上界检查。
- `DUPLICATES` 统计跨来源重复：sparse candidate 命中已被 dense 接受的 `doc_id`。
- filtered candidate 不分配 merged slot。
- final score 是 source score 的 weighted sum，不使用 rank reciprocal。
- `dense_weight_q8 + sparse_weight_q8 == Q8_ONE`。
- 缺失 dense 或 sparse 来源时对应 weighted component 为 0。
- `score_merged_candidates_q8()` 写入 `merged_score_q8`，Top-K 使用该值。

允许的主要数据结构：
- `Candidate`
- `HybridInput`
- `MergedCandidate`
- `HybridState`
- `HybridCounters`

不得新增：
- hash map
- duplicate manager
- RRF object
- scorer object hierarchy

C 函数契约：
- 初始化 candidates/metadata -> `init_data`
- 过滤 candidate -> `passes_filter`
- 线性查找 merged slot -> `find_merged`
- 插入或复用 slot -> `insert_or_find_merged`
- 候选列表 ingest -> `merge_candidate_list`
- score all merged slots -> `score_merged_candidates_q8`
- weighted Q8 score per slot -> `weighted_merge_score_q8`
- final Top-K -> `collect_merged_topk`
- 主流程编排 -> `run_kernel`
- 输出稳定校验 -> `checksum_result`, `print_result`

验证要求：
- `DUPLICATES > 0`
- `FILTERED > 0`
- `OVERFLOW` 必须输出。
- Top-K IDs 有效。
- Top-K 按 `merged_score_q8` 降序，分数相同用更小 `doc_id` tie-break。
- 文档和输出不得声称实现 RRF。

CGRA kernel form:
- 150 指令计划下拆成两个单函数文件：
  - `cgra_kernels/hybrid_merge_ingest_core.c`
  - `cgra_kernels/hybrid_merge_score_topk_core.c`
- dense/sparse candidates、merged table、doc metadata 和输出均使用 flat arrays。
- `hybrid_merge_ingest_core.c` 输出 `merged_doc[]`、`merged_dense_q8[]`、
  `merged_sparse_q8[]`、`merged_mask[]` 和 `out[0] = merged_count`；
  `hybrid_merge_score_topk_core.c` 通过参数接收 `merged_count`，不重新读取原始
  dense/sparse candidate lists。
- 第二阶段的 reference 对比对象是第一阶段输出的 merged flat table，而不是原始
  dense/sparse list。验收时先检查 ingest 的 `merged_count`、`duplicates`、
  `filtered`、`overflow`，再把同一 merged table 喂给 score/topk 检查 weighted score
  和 Top-K。
- `hybrid_merge_score_topk_core.c` 是只读消费中间表的阶段，不得修改 `merged_doc[]`、
  `merged_dense_q8[]`、`merged_sparse_q8[]` 或 `merged_mask[]`。
- 不使用 `continue`/`break`；filter、duplicate 和 overflow 路径用嵌套 `if` 表达。
- host helper `merge_candidate_list`、`find_merged`、`insert_or_find_merged`、
  `score_merged_candidates_q8`、`collect_merged_topk` 在两个单函数 slice 内展开。

CGRA slice boundary:
- `hybrid_merge_ingest_core.c` 覆盖 dense/sparse ingest、metadata filter、
  cross-source duplicate detection、linear merged-table search 和 overflow counter。
- `hybrid_merge_score_topk_core.c` 覆盖 weighted Q8 merge、missing-source score=0
  和 Top-K max-score update。
- 不覆盖 RRF、duplicate bonus、host initialization、input validity checks、checksum、print。

Instruction budget risk:
- 高。当前单函数 266 条，超过 150 阈值；默认按 ingest / score-topk 拆分。

Split policy:
- 必须拆分。若拆分后某个 part 仍超过 150，优先缩小 `MERGE_MAX` 或 Top-K 宽度，
  不删除 filter、duplicate search、overflow 或 weighted merge。
- 旧的 `cgra_kernels/hybrid_merge_core.c` 不得继续作为 `.c` 文件留在 `cgra_kernels/`
  中参与检查；最终目录内只保留拆分后的 150 指令 slice。
- `hybrid_merge_score_topk_core.c` 默认保留 Top-4；若反汇编仍超过 150，允许降为
  Top-2，但必须同步更新输出布局、`reference/haystack_hybrid_merge/analysis*.md`
  和本 mapping，明确这是 CGRA 150 指令 slice 的容量裁剪。

Branch/jump coverage:
- control-flow-heavy。必须覆盖 source branch、filter、duplicate search、overflow 和 Top-K update。

## haystack_context_pack.c

Kernel: `haystack_context_pack.c`

参考归档：
- `reference/haystack_context_pack/source_excerpt.md`
- `reference/haystack_context_pack/analysis.md`
- `reference/haystack_context_pack/analysis_zh.md`

参考来源明细：
- Repo: `deepset-ai/haystack`
  - File: `haystack/components/builders/prompt_builder.py`
  - Function / method: `PromptBuilder.run()`
  - URL: `https://github.com/deepset-ai/haystack/blob/main/haystack/components/builders/prompt_builder.py`
  - 提供语义：documents/query 作为 template variables 被消费。
- Docs: Haystack PromptBuilder
  - File / page: PromptBuilder docs
  - Function / method: retriever -> PromptBuilder pipeline usage
  - URL: `https://docs.haystack.deepset.ai/docs/promptbuilder`
  - 提供语义：retrieved documents 进入 generator 前的 pipeline 位置。

来源边界：
- Haystack `PromptBuilder.run()` 只提供 documents/query 被消费进入 prompt rendering
  的 pipeline 位置。
- Benchmark 自己定义 score sort、source/chunk dedup、token budget packing 和
  truncation policy。
- 该 kernel 是 PromptBuilder-adjacent context preparation，不是 PromptBuilder 模板引擎。

明确不实现：
- Jinja template rendering
- string prompt construction
- tokenizer-dependent token counting
- real document text
- LLM/generator call

C 行为：
- 对 deterministic candidates 按 score 降序排序。
- host reference 保留 deterministic candidate list 的排序和 packing 行为。
- 150 指令 CGRA slice 可将固定容量窗口缩小为 `CGRA_CONTEXT_K = 4`。进入 CGRA
  的输入必须已经是 host 预选或裁剪后的候选窗口；CGRA 只负责这个窗口内部的
  score ordering、dedup、budget 和 truncate 行为。不能把完整 host candidate list 的
  最终 packing 结果直接作为该 slice 的逐项匹配目标。
- host 预选窗口必须 deterministic；测试必须固定窗口内容和输入顺序，使 slice
  行为可复现。
- 分数相同用更小 `doc_id` tie-break。
- 已 packed 的 `(source_id, chunk_id)` 再出现时跳过。
- full token_len fits 时完整加入。
- remaining budget 大于等于 `MIN_TRUNC_TOKENS` 时可 truncation。
- truncated packed doc 使用全部 remaining budget，`truncated = 1`。
- 否则增加 `SKIPPED_BUDGET`。

允许的主要数据结构：
- `ContextCandidate`
- `PackedDoc`
- `ContextPackInput`
- `ContextPackResult`
- `ContextPackCounters`

不得新增：
- template renderer
- string buffer state
- token-budget manager object

C 函数契约：
- 初始化 candidates -> `init_data`
- 结果和 counters 初始化 -> `reset_result`
- score sort -> `sort_candidates_by_score`
- duplicate predicate -> `is_duplicate_source_chunk`
- budget 查询 -> `remaining_budget`
- packed output append -> `append_packed_doc`
- full/truncated/skip 决策 -> `pack_candidate_with_budget`
- 主流程编排 -> `run_kernel`
- 输出稳定校验 -> `checksum_result`, `print_result`

验证要求：
- packed IDs 有效。
- `TRUNCATED + SKIPPED_DUPLICATE + SKIPPED_BUDGET > 0`
- `USED_TOKENS <= TOKEN_BUDGET`
- `TOKEN_BUDGET > 0` 且 `0 < MIN_TRUNC_TOKENS <= TOKEN_BUDGET`。
- truncated entry 的 `used_tokens` 等于 append 前的 remaining budget。
- duplicate 检查基于 `(source_id, chunk_id)`，不是 `doc_id`。
- checksum 覆盖 packed IDs、used tokens、counters。
- 若验证 CGRA 150 指令 slice，reference 必须先裁剪到同一个 `CGRA_CONTEXT_K = 4`
  窗口，再比较窗口内排序、duplicate、budget、truncate 和 counters。
- 该固定窗口应由 host harness deterministic 产生；窗口选择本身不属于 CGRA slice
  行为匹配范围。

CGRA kernel form:
- `cgra_kernels/context_pack_core.c`
- 单文件单函数；candidate fields 使用 flat arrays，packed output 写入 `out[]`。
- 150 指令计划下将候选窗口缩小到 4 或 5，优先 4。
- `count > CGRA_CONTEXT_K` 时的窗口外候选不属于 CGRA slice 行为匹配范围；由 host
  harness 负责预选窗口或记录容量 overflow。
- 不使用 `continue`/`break`；duplicate skip、budget skip 和 no-best path 用嵌套
  `if` 表达。
- host helper `sort_candidates_by_score`、`is_duplicate_source_chunk`、
  `pack_candidate_with_budget`、`append_packed_doc` 在单函数内展开。

CGRA slice boundary:
- 覆盖 score ordering、source/chunk duplicate skip、token budget full append、truncate、
  skip-budget 分支。
- 不覆盖 Jinja、字符串、tokenizer、host checksum、print。

Instruction budget risk:
- 中高。当前 158 条，略高于 150 阈值；优先缩小固定窗口并减少临时数组搬移，
  不删除 ordering、duplicate 或 budget 分支。

Split policy:
- 默认不拆分。若窗口缩小和控制流改写后仍超 150，可改成更小 fixed-K selection pass，
  但必须保留 score-ordering 语义和 tie-break。

Branch/jump coverage:
- control-flow-heavy。实现代码必须保留 duplicate skip、full append、truncate 和
  budget skip 四类路径；单个输入样例不必同时触发所有路径，但测试集必须覆盖这些
  关键分支。

## haystack_lexrank.c

Kernel: `haystack_lexrank.c`

参考归档：
- `reference/haystack_lexrank/source_excerpt.md`
- `reference/haystack_lexrank/analysis.md`
- `reference/haystack_lexrank/analysis_zh.md`

参考来源明细：
- Repo: `crabcamp/lexrank`
  - File: `README.rst`
  - Function / method: similarity-matrix centrality and ranking examples
  - URL: `https://github.com/crabcamp/lexrank/blob/dev/README.rst`
  - 提供语义：LexRank algorithm overview 和 centrality/ranking behavior 背景。
- Repo: `miso-belica/sumy`
  - File: `sumy/summarizers/lex_rank.py`
  - Function / method: LexRank summarizer
  - URL: `https://github.com/miso-belica/sumy/blob/main/sumy/summarizers/lex_rank.py`
  - 提供语义：similarity matrix、threshold graph、power-method ranking、selection。
- Docs: NetworkX PageRank
  - File / page: `pagerank_alg.pagerank` docs
  - Function / method: `pagerank()`
  - URL: `https://networkx.org/documentation/stable/reference/algorithms/generated/networkx.algorithms.link_analysis.pagerank_alg.pagerank.html`
  - 提供语义：damping、incoming rank propagation、iterative update、dangling 背景。

来源边界：
- `crabcamp/lexrank` 和 `sumy` 提供 sentence similarity graph、centrality、selection
  语义。
- NetworkX PageRank 提供 damping、incoming rank propagation、iterative update
  背景。
- Benchmark 定义 deterministic sentence-term matrix、Q8 similarity/rank、fixed
  iteration、dangling redistribution 和 redundancy filter。

明确不实现：
- tokenizer
- stop-word processing
- strict cosine，除非 C 代码真的实现 strict cosine
- NetworkX graph API
- convergence tolerance
- full text summarization pipeline

C 行为：
- 计算 sentence similarity matrix。
- 构建 threshold graph；必须说明 `EDGES` 是 undirected pairs 还是 directed stored
  adjacency entries。
- 初始化 rank 为 `1 / N`。
- fixed iteration 执行 PageRank-style update。
- `out_degree == 0` 时按 dangling redistribution policy 处理：
  `dangling_sum / N` 加到每个 destination 的 incoming。
- Q8 rank propagation 使用 `rank_old_q8`、`rank_new_q8`、`damping_q8`、`base_q8`，
  truncation toward zero，fixed iterations 后不 renormalize。
- Graph construction 使用 `sim_q8 >= SIM_THRESHOLD_Q8`。
- 按 rank 选择 summary sentences，并在插入前做 redundancy check。

允许的主要数据结构：
- `SentenceCorpus`
- `LexRankState`
- `LexRankResult`
- `LexRankCounters`

不得新增：
- graph API wrapper
- matrix wrapper object
- ranker object
- tokenizer/corpus pipeline object

C 函数契约：
- 初始化 sentence-term matrix -> `init_corpus`
- 初始化 graph/rank/result/counters -> `reset_state`
- similarity -> `sentence_sim_q8`
- similarity matrix -> `compute_similarity_matrix`
- threshold graph -> `build_threshold_graph`
- fixed PageRank-style iterations -> `run_rank_iterations`
- redundancy predicate -> `sentence_is_redundant`
- summary selection -> `select_summary_sentences`
- 主流程编排 -> `run_kernel`
- 输出稳定校验 -> `checksum_result`, `print_result`

验证要求：
- `EDGES > 0`
- `ITERATIONS == FIXED_ITERATIONS`
- selected sentence IDs 有效。
- dangling policy 在代码中可见，且不得除以 0。
- rank initialization、damping、base term、Q8 truncation policy 可见。
- checksum 覆盖 selected IDs、rank-derived scores 或 selected order、edges、iterations、
  redundancy counter。

CGRA kernel form:
- 第一阶段为 `cgra_kernels/lexrank_rank_core.c`。
- 完整覆盖需要拆成 `lexrank_sim_graph_core.c`、`lexrank_rank_core.c`、
  `lexrank_select_core.c`。

CGRA slice boundary:
- `lexrank_rank_core.c` 只匹配 PageRank-style iterative update block。
- 它覆盖 dangling_sum、incoming edge scan、Q8 damping update、fixed iterations。
- 它不覆盖 sentence similarity matrix、threshold graph construction 或 redundancy selection。

Instruction budget risk:
- 高。完整 LexRank pipeline 大概率超过理论 576 条上限；在当前 150 条 practical
  target 下必须按阶段拆分。

Split policy:
- 优先实现 `lexrank_rank_core.c`。
- 如果需要完整 pipeline，必须拆成多个单函数文件，并为每个文件记录对应 reference
  pseudocode block。

Branch/jump coverage:
- control-flow-heavy。`lexrank_rank_core.c` 必须覆盖 dangling node detection、
  incoming edge scan 和 fixed rank iteration。
