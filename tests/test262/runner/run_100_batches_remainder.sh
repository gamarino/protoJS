#!/usr/bin/env bash
# Remainder of run_100_batches: patterns 73-100 (28 batches)
set -e
REPO=/home/gamarino/Documentos/proyectos/protoJS
CONFIG="$REPO/tests/test262/config/test262_paths.json"
RUNNER="$REPO/tests/test262/runner/test262_runner.js"
ADD_ROW="$REPO/tests/test262/runner/add_conformance_row.js"
export PROTOJS=$REPO/build/protojs
export TEST262_ROOT=/home/gamarino/Documentos/proyectos/test262
cd "$REPO"

patterns=(
  "built-ins/RegExp"
  "built-ins/RegExp/prototype"
  "built-ins/RegExp/prototype/exec"
  "built-ins/RegExp/prototype/test"
  "built-ins/Promise"
  "built-ins/Promise/all"
  "built-ins/Promise/prototype"
  "built-ins/Promise/prototype/then"
  "built-ins/Promise/resolve"
  "built-ins/Map"
  "built-ins/Map/prototype"
  "built-ins/Map/prototype/get"
  "built-ins/Map/prototype/set"
  "built-ins/Set"
  "built-ins/Set/prototype"
  "built-ins/Set/prototype/add"
  "built-ins/Set/prototype/has"
  "built-ins/WeakMap/prototype"
  "built-ins/WeakSet/prototype"
  "built-ins/Proxy/get"
  "built-ins/Proxy/set"
  "built-ins/Reflect/get"
  "built-ins/Reflect/set"
  "built-ins/Reflect/apply"
  "built-ins/Iterator/from"
  "built-ins/Iterator/prototype"
  "built-ins/GeneratorFunction"
  "built-ins/GeneratorFunction/prototype"
)

for p in "${patterns[@]}"; do
  node -e "
  const fs=require('fs');
  const cfg=JSON.parse(fs.readFileSync('$CONFIG','utf8'));
  cfg.patterns=['$p'];
  fs.writeFileSync('$CONFIG', JSON.stringify(cfg, null, 2));
  "
  out=$(node "$RUNNER" 2>&1) || true
  snap=$(echo "$out" | grep -o 'tests/test262/reports/snapshot-[^ ]*\.json' | tail -1)
  if [ -z "$snap" ] || [ ! -f "$REPO/$snap" ]; then echo "No snapshot for $p"; exit 1; fi
  read -r total passed fs fm to <<< $(node -e "const s=require('./$snap'); console.log(s.total, s.summary.passed, s.summary.failed_syntax, s.summary.failed_semantics, s.summary.timeout)")
  node "$ADD_ROW" "$p" "$total" "$passed" "$fs" "$fm" "$to"
  git add CONFORMANCE_JS.md "$CONFIG" "$snap"
  git commit -m "docs(test262): record $p conformance"
  echo "Done: $p ($total tests)"
done
echo "Remainder done."
