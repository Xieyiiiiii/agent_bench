#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require_pattern() {
  local pattern="$1"
  local path="$2"
  if ! grep -Eq "$pattern" "$path"; then
    echo "missing pattern '$pattern' in $path" >&2
    exit 1
  fi
}

if [[ ! -d "$ROOT_DIR/cgra_kernels" ]]; then
  echo "missing cgra_kernels directory" >&2
  exit 1
fi

shopt -s nullglob
files=("$ROOT_DIR"/cgra_kernels/*.c)
if [[ "${#files[@]}" -eq 0 ]]; then
  echo "no CGRA kernel files found" >&2
  exit 1
fi

for src in "${files[@]}"; do
  require_pattern 'CGRA kernel slice' "$src"
  require_pattern 'Reference slice:' "$src"
  require_pattern 'Output layout:' "$src"

  function_count="$(grep -Ec '^[[:space:]]*int[[:space:]]+[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\(' "$src")"
  if [[ "$function_count" -ne 1 ]]; then
    echo "$src must contain exactly one int function definition, got $function_count" >&2
    exit 1
  fi

  if grep -Eq '(^|[^A-Za-z0-9_])main[[:space:]]*\(' "$src"; then
    echo "$src must not define main" >&2
    exit 1
  fi
  if grep -Eq '#[[:space:]]*include[[:space:]]*<stdio\.h>|printf[[:space:]]*\(|puts[[:space:]]*\(|fprintf[[:space:]]*\(|malloc[[:space:]]*\(|free[[:space:]]*\(|memcpy[[:space:]]*\(' "$src"; then
    echo "$src contains forbidden runtime or I/O usage" >&2
    exit 1
  fi
  if grep -Eq 'typedef[[:space:]]+struct|struct[[:space:]]+[A-Za-z_]' "$src"; then
    echo "$src must use flat arrays/scalars, not structs" >&2
    exit 1
  fi
done

echo "check_cgra_shape: ok"
