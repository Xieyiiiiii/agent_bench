# CGRA Single-function Kernel Slices

This directory contains CGRA frontend-friendly kernel slices derived from the
host reference benchmarks in `../src/`.

Hardware/compiler constraints:

- One C file contains exactly one function.
- No `main()`.
- No helper function calls.
- No `printf` or standard I/O.
- No dynamic allocation or string/runtime helpers.
- Outputs are written through caller-provided buffers.
- Each function targets at most `6 * 6 * 16 = 576` disassembled instructions.
- Runtime inputs must already be sanitized by the host harness where a slice has
  no `num_docs` or vocabulary-size parameter. For example, dense/sparse
  `doc_id` values must index valid metadata arrays before entering
  `hybrid_merge_core`.
- Fixed-capacity slices process only their documented window. For
  `context_pack_core`, `count` is capped to `CGRA_CONTEXT_K`; extra candidates
  are host-side capacity overflow and are outside this single-function slice.

These files are not replacements for the host reference benchmarks. The host
versions remain in `../src/` for readable algorithm flow and regression tests.
The CGRA files keep the most important workload slice documented in
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

If a kernel exceeds the instruction budget or produces a call-like instruction,
reduce the slice, shrink fixed constants, or split the workload into multiple
single-function files and update the mapping documentation before changing the
C behavior.
