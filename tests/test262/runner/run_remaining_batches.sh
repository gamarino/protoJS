#!/usr/bin/env bash
# Run all remaining Test262 batches; document each in CONFORMANCE_JS.md; single commit at end.
set -e
REPO=/home/gamarino/Documentos/proyectos/protoJS
CONFIG="$REPO/tests/test262/config/test262_paths.json"
RUNNER="$REPO/tests/test262/runner/test262_runner.js"
ADD_ROW="$REPO/tests/test262/runner/add_conformance_row.js"
REMAINING="${1:-/tmp/remaining.txt}"
SNAPSHOTS_LIST="/tmp/snapshots_remaining_$$.txt"
> "$SNAPSHOTS_LIST"
export PROTOJS=$REPO/build/protojs
export TEST262_ROOT=/home/gamarino/Documentos/proyectos/test262
cd "$REPO"

if [ ! -f "$REMAINING" ]; then
  echo "Usage: $0 [path-to-remaining-patterns.txt]"
  echo "Generate remaining list with: comm -23 <(all built-ins dirs) <(grep from CONFORMANCE_JS.md)"
  exit 1
fi

count=$(wc -l < "$REMAINING")
echo "Running $count remaining batches (no per-batch commit)."

n=0
while IFS= read -r p || [ -n "$p" ]; do
  [ -z "$p" ] && continue
  n=$((n + 1))
  printf "[%d/%d] %s\n" "$n" "$count" "$p"
  node -e "
  const fs = require('fs');
  const p = process.argv[1];
  const cfg = JSON.parse(fs.readFileSync(process.argv[2], 'utf8'));
  cfg.patterns = [p];
  fs.writeFileSync(process.argv[2], JSON.stringify(cfg, null, 2));
  " "$p" "$CONFIG"
  out=$(node "$RUNNER" 2>&1) || true
  snap=$(echo "$out" | grep -o 'tests/test262/reports/snapshot-[^ ]*\.json' | tail -1)
  if [ -z "$snap" ] || [ ! -f "$REPO/$snap" ]; then
    echo "  No snapshot for $p — skipping row"
    continue
  fi
  echo "$snap" >> "$SNAPSHOTS_LIST"
  read -r total passed fs fm to <<< $(node -e "const s=require('$REPO/$snap'); console.log(s.total, s.summary.passed, s.summary.failed_syntax, s.summary.failed_semantics, s.summary.timeout)")
  node "$ADD_ROW" "$p" "$total" "$passed" "$fs" "$fm" "$to"
done < "$REMAINING"

echo "All batches run. Staging and single commit..."
git add CONFORMANCE_JS.md "$CONFIG"
while IFS= read -r s; do
  [ -f "$REPO/$s" ] && git add "$s"
done < "$SNAPSHOTS_LIST"
git status --short
git commit -m "docs(test262): record remaining batches conformance (single run)"
echo "Done. Commit created."
rm -f "$SNAPSHOTS_LIST"
