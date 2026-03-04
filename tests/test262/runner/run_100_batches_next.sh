#!/usr/bin/env bash
# Run next 100 Test262 batches from pattern list. Doc + commit each.
# Usage: run_100_batches_next.sh [pattern_file]
# Default pattern file: next100_batches.txt in same dir (or pass path).
set -e
REPO=/home/gamarino/Documentos/proyectos/protoJS
CONFIG="$REPO/tests/test262/config/test262_paths.json"
RUNNER="$REPO/tests/test262/runner/test262_runner.js"
ADD_ROW="$REPO/tests/test262/runner/add_conformance_row.js"
PATTERN_FILE="${1:-$REPO/tests/test262/runner/next100_batches.txt}"
export PROTOJS=$REPO/build/protojs
export TEST262_ROOT=/home/gamarino/Documentos/proyectos/test262
cd "$REPO"

if [ ! -f "$PATTERN_FILE" ]; then
  echo "Pattern file not found: $PATTERN_FILE"
  exit 1
fi

count=0
while IFS= read -r p || [ -n "$p" ]; do
  [ -z "$p" ] && continue
  count=$((count + 1))
  node "$REPO/tests/test262/runner/update_config_pattern.js" "$p"
  out=$(node "$RUNNER" 2>&1) || true
  snap=$(echo "$out" | grep -o 'tests/test262/reports/snapshot-[^ ]*\.json' | tail -1)
  if [ -z "$snap" ] || [ ! -f "$REPO/$snap" ]; then echo "No snapshot for $p"; exit 1; fi
  read -r total passed fs fm to <<< $(node -e "const s=require('./$snap'); console.log(s.total, s.summary.passed, s.summary.failed_syntax, s.summary.failed_semantics, s.summary.timeout)")
  node "$ADD_ROW" "$p" "$total" "$passed" "$fs" "$fm" "$to"
  git add CONFORMANCE_JS.md "$CONFIG" "$snap"
  git commit -m "docs(test262): record $p conformance"
  echo "Done [$count/100]: $p ($total tests)"
done < "$PATTERN_FILE"
echo "All 100 batches done."
