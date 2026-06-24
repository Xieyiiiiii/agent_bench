#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

kernels=(
  haystack_enns_flat
  haystack_enns_filtered
  haystack_bm25
  haystack_hybrid_merge
  haystack_context_pack
  haystack_lexrank
)

for kernel in "${kernels[@]}"; do
  bin="$ROOT_DIR/build/$kernel"
  if [[ ! -x "$bin" ]]; then
    echo "missing executable: $bin" >&2
    exit 1
  fi
  "$bin" > "$ROOT_DIR/build/$kernel.out"
  grep -q '^KERNEL=' "$ROOT_DIR/build/$kernel.out"
  grep -q '^CHECKSUM=' "$ROOT_DIR/build/$kernel.out"
done

echo "run_all: ok"
