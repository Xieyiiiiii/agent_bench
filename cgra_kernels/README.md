# CGRA Single-function Kernel Slices

This directory contains CGRA frontend-friendly kernel slices derived from the
host reference benchmarks in `../src/`.

Current kernel-form constraints:

- One C file contains exactly one function.
- No `main()`.
- No helper function calls; the current frontend cannot lower calls in a CGRA
  slice.
- No `printf` or standard I/O.
- No dynamic allocation or string/runtime helpers.
- Outputs are written through caller-provided buffers.
- The hardware can hold at most `6 * 6 * 16 = 576` instructions, but routing
  nodes reduce the practical kernel budget. The repository therefore uses
  150 disassembled instructions per CGRA function as the implementation target.
- The current frontend cannot lower `continue` or `break`; use nested `if`
  blocks and simple state flags instead.
- The flat-array interface is a project-level convention enforced by
  `tests/check_cgra_shape.sh` to keep the compiler-facing slice simple and
  deterministic. It should not be read as a CGRA architectural property.
- Runtime inputs must already be sanitized by the host harness where a slice has
  no `num_docs` or vocabulary-size parameter. For example, dense/sparse
  `doc_id` values must index valid metadata arrays before entering
  `hybrid_merge_ingest_core`.
- Fixed-capacity slices process only their documented window. For
  `context_pack_core`, the input must already be the host-selected window and
  `count` is capped to `CGRA_CONTEXT_K`; extra candidates are host-side capacity
  overflow and are outside this single-function slice. The selected window must
  be deterministic for reproducible reference comparison.

These files are not replacements for the host reference benchmarks. The host
versions remain in `../src/` for readable algorithm flow and regression tests.
The CGRA files keep the important, independently verifiable algorithm stage documented in
`../ref/kernel_reference_mapping.md` and `../reference/<kernel>/analysis*.md`.

Run:

```bash
make cgra-check
```

`scripts/count_instructions.sh` uses freestanding-style GCC flags by default:
`-ffreestanding -fno-builtin -fno-stack-protector
-fno-tree-loop-distribute-patterns`. This prevents host GCC from turning simple
loops into libcalls such as `memset` or stack-protector hooks. For a target CGRA
compiler, set `CGRA_CC`, `CGRA_OBJDUMP`, and optionally `CGRA_CFLAGS`; any
remaining `call`/`bl`/`jal`-style instruction is treated as a violation.

If a kernel exceeds the practical instruction budget, uses unsupported control
flow, or produces a call-like instruction, reduce the slice, shrink fixed
constants, or split the algorithm into multiple single-function files and update
the mapping documentation before changing the C behavior. When a workload is
split, the old oversized `.c` file must be removed from this directory or moved
out of the `*.c` check set. The current implementation splits hybrid merge into
`hybrid_merge_ingest_core.c` and `hybrid_merge_score_topk_core.c`, keeps each
remaining CGRA function within the active instruction target, and preserves the
readable host reference benchmarks in `../src/`.
