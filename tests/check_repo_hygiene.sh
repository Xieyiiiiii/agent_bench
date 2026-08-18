#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

require_file() {
  if [[ ! -f "$1" ]]; then
    echo "missing required repository file: $1" >&2
    exit 1
  fi
}

require_file "$ROOT_DIR/SCHEDULING_ROBOTICS_BACKGROUND_ZH.md"
require_file "$ROOT_DIR/SCHEDULING_ROBOTICS_CODE_SUMMARY_ZH.md"
require_file "$ROOT_DIR/reference_papers/README.md"

if [[ -f "$ROOT_DIR/SCHEDULING_ROBOTICS_WORKLOAD_REPORT_ZH.md" ]]; then
  echo "obsolete scheduling/robotics index document is still present" >&2
  exit 1
fi

if ! grep -Fxq 'reference_papers/**/*.pdf' "$ROOT_DIR/.gitignore"; then
  echo "reference paper PDFs must be ignored" >&2
  exit 1
fi

if git -C "$ROOT_DIR" ls-files -- '*.pdf' | grep -q .; then
  echo "PDF files must not be tracked" >&2
  exit 1
fi

if git -C "$ROOT_DIR" ls-files -- 'build/**' '*.o' '*.out' '*.log' | grep -q .; then
  echo "generated build files must not be tracked" >&2
  exit 1
fi

if grep -RIEq --include='*.md' '小核|small kernel' "$ROOT_DIR"; then
  echo "ambiguous small-kernel terminology found in documentation" >&2
  exit 1
fi

if grep -RIEq --exclude-dir='.git' --exclude='check_repo_hygiene.sh' \
    'COMPLETION_ORDER|GPU_REJECTED|SCHEDULING_ROBOTICS_WORKLOAD_REPORT_ZH' \
    "$ROOT_DIR"; then
  echo "stale scheduling contract or obsolete document reference found" >&2
  exit 1
fi

echo "check_repo_hygiene: ok"
