#!/usr/bin/env bash
# Run N Test262 batches: for each pattern, update config, run runner, append row to CONFORMANCE_JS.md, commit.
# Usage: run_batches.sh [pattern1 pattern2 ...]
# Requires: PROTOJS and TEST262_ROOT set (or defaults in runner).

set -e
REPO=/home/gamarino/Documentos/proyectos/protoJS
CONFIG="$REPO/tests/test262/config/test262_paths.json"
DOC="$REPO/CONFORMANCE_JS.md"
MARKER="The \`built-ins/Object\` mini-suite"

cd "$REPO"
patterns=("$@")
for p in "${patterns[@]}"; do
  # Update config
  node -e "
  const fs=require('fs');
  const cfg=JSON.parse(fs.readFileSync('$CONFIG','utf8'));
  cfg.patterns=['$p'];
  fs.writeFileSync('$CONFIG', JSON.stringify(cfg, null, 2));
  "
  # Run runner; capture snapshot path
  out=$(PROTOJS=./build/protojs TEST262_ROOT=/home/gamarino/Documentos/proyectos/test262 node tests/test262/runner/test262_runner.js 2>&1)
  snap=$(echo "$out" | grep -o 'tests/test262/reports/snapshot-[^ ]*\.json' | tail -1)
  if [ -z "$snap" ] || [ ! -f "$snap" ]; then echo "No snapshot for $p"; exit 1; fi
  # Get summary
  read -r total passed fs fm to <<< $(node -e "const s=require('./$snap'); console.log(s.total, s.summary.passed, s.summary.failed_syntax, s.summary.failed_semantics, s.summary.timeout)")
  # Folder label for table (escape / for display)
  label=$(echo "$p" | sed 's|/|/|g')
  # Append row before MARKER (use a single line for the row)
  row="| \`$p\` | $total | $passed | $fs | $fm | $to | Official Test262 subset. |"
  # Insert row: add before the line that contains "The \`built-ins/Object\` mini-suite"
  sed -i "/$MARKER/i $row" "$DOC"
  # Commit
  git add CONFORMANCE_JS.md "$CONFIG" "$snap"
  git commit -m "docs(test262): record $p conformance"
  echo "Done: $p ($total tests)"
done
echo "All ${#patterns[@]} batches done."
