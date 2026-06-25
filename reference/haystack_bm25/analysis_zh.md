# haystack_bm25 中文分析

## 为什么抽取这段参考行为

sparse retrieval 的 CPU 热路径是 posting-list traversal 和 per-posting score
accumulation。Haystack 提供 query/filter/top_k 的 retriever flow，`rank_bm25`
提供 BM25Okapi scoring 公式，`bm25s` 提供 posting-list / sparse layout 的组织
思路。benchmark 只抽取 scoring 和 accumulation workload，不抽取 tokenizer、
corpus preprocessing 或 Python object model。

在 CGRA benchmark 场景中，这个 kernel 只保留 deterministic posting traversal
和 fixed-point scoring。知识库构建、分词、语料统计和运行时 IDF 计算仍属于
CPU/应用侧预处理，不放入当前可灌入芯片的小核。

## C 实现目标

需要保留：

- deterministic query term IDs；
- flat postings array 和 posting-list offset；
- document-length normalized BM25Okapi score；
- deterministic synthetic `idf_q8[]` 输入；
- metadata filtering；
- active document tracking，然后再做 Top-K。

不需要保留：

- tokenizer 和 corpus preprocessing；
- `BM25Okapi._calc_idf()` 以及 negative-IDF epsilon floor；
- Python dictionaries 或 NumPy arrays；
- Haystack 完整 in-memory store；
- bm25s CSR 内部实现细节。

## CPU 瓶颈分析

主要瓶颈是 posting list 的不规则内存访问、固定点整数评分、filter 分支和最后的
active-doc Top-K 收集。它的性能不由连续向量扫描主导，而由 query terms 对应的
posting 分布、filtered postings 比例、score accumulation 写入同一 doc 的局部性决定。
这个 kernel 衡量 lexical retrieval scoring，不衡量分词或文本解析。
也就是说，瓶颈是 accepted posting stream 和 score accumulation，不是知识库构建。

## 面向过程实现形态

函数链应把 index setup、posting traversal、scoring、Top-K collection 分开：

```text
main
  init_index
  init_query
  reset_state
  run_kernel
    score_posting_list_q8
      passes_filter
      bm25_term_score_q8
    collect_active_docs_into_topk
  checksum_result
  print_result
```

使用一个 `Bm25Index`、一个 `Bm25Query`、一个 `Bm25State`、一个 `Bm25Counters`。
不要为 postings、scores、active flags 再增加额外 wrapper 结构。

## 行为匹配检查

- empty posting list 必须增加 `EMPTY_TERMS`。
- `idf_q8[]` 必须视为 `init_index()` 生成的 deterministic input，不在 scoring loop 中重算。
- Q8 scoring 必须使用 `source_excerpt.md` 中的 fixed-point contract，包括 int64
  intermediates、toward-zero truncation 和 zero-denominator 处理。
- filtered postings 不得影响 score 或 active flag。
- 只有至少一个 accepted posting 对文档贡献分数后，该文档才变为 active。
- 最终 Top-K 必须从 active documents 收集，而不是遍历所有文档直接比较。
- 表示 BM25 fixed-point arithmetic 的变量必须使用 `_q8` 后缀。

## CGRA 单函数实现边界

CGRA 版本优先对应 `bm25_score_core.c`，只保留 posting-list traversal、filter branch、
Q8 term scoring、score accumulation 和 active flag update。host 版本中的
`init_index`、`init_query`、`score_posting_list_q8`、`passes_filter`、
`bm25_term_score_q8`、`collect_active_docs_into_topk` 等函数调用都必须内联或拆到单独
CGRA 文件中；第一阶段不强制把 Top-K collection 放入同一个函数。

```text
bm25_score_core
  遍历 query terms
    读取 posting-list start/len
    empty list 增加 empty_terms
    遍历 postings
      metadata filter 失败则增加 filtered_out
      accepted posting 执行 Q8 score contribution
      累加 score_q8[doc] 并设置 active[doc]
  统计 accepted_postings 和 active_docs_after_scoring
  写回 branch counters
```

host reference 的 BM25 Q8 契约保留 `int64 intermediates`，用于说明数学语义和避免
host 侧溢出。但 CGRA slice 受“无隐式函数调用”约束，不能盲目使用会被后端降成 runtime
helper call 的 64-bit multiply/divide。CGRA 实现应优先通过受控输入范围保持 32-bit
运算安全；如果 CGRA 工具链反汇编显示除法或宽乘法产生 call-like 指令，则必须进一步
摘取 scoring slice、调整常量范围，或拆分文件，并在 mapping 中说明该 CGRA slice 与
host Q8 契约的差异。

输出 buffer 必须使用确定语义：`accepted_postings`、`filtered_out`、`empty_terms`、
`active_docs_after_scoring`。不得使用 estimate 或 touched-doc 这类模糊 counter。该
slice 是 control-flow-heavy kernel，复杂分支来自 query term loop、empty list、posting
filter、accepted accumulation 和 active flag。
