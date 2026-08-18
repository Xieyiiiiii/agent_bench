#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CC_BIN="${CGRA_CC:-${CC:-gcc}}"
OBJDUMP_BIN="${CGRA_OBJDUMP:-${OBJDUMP:-objdump}}"
MAX_INSTRUCTIONS="${CGRA_MAX_INSTRUCTIONS:-150}"
CGRA_CFLAGS_DEFAULT="-std=c99 -Wall -Wextra -Werror -O2 -ffreestanding -fno-builtin -fno-stack-protector -fno-tree-loop-distribute-patterns"
CGRA_CFLAGS_BIN="${CGRA_CFLAGS:-$CGRA_CFLAGS_DEFAULT}"
OUT_DIR="$ROOT_DIR/build/cgra"

mkdir -p "$OUT_DIR"

status=0
for src in "$ROOT_DIR"/cgra_kernels/*.c; do
  base="$(basename "$src" .c)"
  obj="$OUT_DIR/$base.o"
  dump="$OUT_DIR/$base.dump"

  # Default to a freestanding accelerator-style audit: no libcalls generated
  # from builtins, stack-protector hooks, or loop-to-memset transforms.
  # Target toolchains can override this with CGRA_CFLAGS.
  # shellcheck disable=SC2086
  "$CC_BIN" $CGRA_CFLAGS_BIN -c "$src" -o "$obj"
  "$OBJDUMP_BIN" -d "$obj" > "$dump"

  function_name="$(awk '/^[[:space:]]*int[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\(/ {
    line=$0
    sub(/^[[:space:]]*int[[:space:]]+/, "", line)
    sub(/[[:space:]]*\(.*/, "", line)
    print line
    exit
  }' "$src")"

  instructions="$(awk '
    /^[[:xdigit:]]+ <.*>:/ { in_fn=1; next }
    in_fn && /^[[:space:]]*[[:xdigit:]]+:/ { count++ }
    END { print count + 0 }
  ' "$dump")"

  call_count="$(awk '
    /^[[:xdigit:]]+ <.*>:/ { in_fn=1; next }
    in_fn && /^[[:space:]]*[[:xdigit:]]+:/ &&
      $0 ~ /[[:space:]](call[a-z]*|bl|blx|jal|jalr)[[:space:]]/ { count++ }
    END { print count + 0 }
  ' "$dump")"

  printf '%s function=%s instructions=%s calls=%s\n' \
    "${src#$ROOT_DIR/}" "$function_name" "$instructions" "$call_count"

  if [[ "$instructions" -gt "$MAX_INSTRUCTIONS" ]]; then
    echo "instruction budget exceeded: ${src#$ROOT_DIR/} has $instructions > $MAX_INSTRUCTIONS" >&2
    status=1
  fi
  if [[ "$call_count" -ne 0 ]]; then
    echo "call-like instruction found: ${src#$ROOT_DIR/}" >&2
    status=1
  fi
done

exit "$status"
