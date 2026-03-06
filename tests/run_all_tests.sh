#!/usr/bin/env bash
# Run C++ unit tests, smoke test, and optionally Phase 6 + Test262.
# Exit non-zero if any step fails.
# Usage: from repo root: ./tests/run_all_tests.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

PROTOJS="${PROTOJS:-$REPO_ROOT/build/protojs}"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build}"

echo "[run_all_tests] Building..."
cmake --build "$BUILD_DIR"

echo "[run_all_tests] C++ unit tests (excluding .integration / .network)..."
cd "$BUILD_DIR"
ctest --output-on-failure -E "integration|network"
cd "$REPO_ROOT"

echo "[run_all_tests] Smoke (protoCore path)..."
PROTOJS_USE_PROTO_EVAL=1 PROTOJS="$PROTOJS" node tests/test262/runner/proto_eval_smoke.js

echo "[run_all_tests] Phase 6 directed test..."
PROTOJS_USE_PROTO_EVAL=1 "$PROTOJS" --proto-eval tests/test262/tests/phase6_native_global.js

if [ -n "$TEST262_ROOT" ] && [ -d "$TEST262_ROOT" ]; then
  echo "[run_all_tests] Test262 (pattern from config, protoCore path)..."
  TEST262_USE_PROTO_EVAL=1 PROTOJS="$PROTOJS" node tests/test262/runner/test262_runner.js
else
  echo "[run_all_tests] Skipping Test262 (set TEST262_ROOT to run)"
fi

echo "[run_all_tests] All steps passed."
