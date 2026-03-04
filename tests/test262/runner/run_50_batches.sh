#!/usr/bin/env bash
set -e
REPO=/home/gamarino/Documentos/proyectos/protoJS
CONFIG="$REPO/tests/test262/config/test262_paths.json"
DOC="$REPO/CONFORMANCE_JS.md"
RUNNER="$REPO/tests/test262/runner/test262_runner.js"
ADD_ROW="$REPO/tests/test262/runner/add_conformance_row.js"
export PROTOJS=$REPO/build/protojs
export TEST262_ROOT=/home/gamarino/Documentos/proyectos/test262
cd "$REPO"

# Batches 4-50 (47 patterns)
patterns=(
  "built-ins/Array/from"
  "built-ins/Array/of"
  "built-ins/Array/length"
  "built-ins/Array/Symbol.species"
  "built-ins/ArrayIteratorPrototype"
  "built-ins/ArrayIteratorPrototype/next"
  "built-ins/ArrayIteratorPrototype/Symbol.toStringTag"
  "built-ins/ArrayBuffer/isView"
  "built-ins/ArrayBuffer/Symbol.species"
  "built-ins/Boolean"
  "built-ins/BigInt"
  "built-ins/Number"
  "built-ins/Number/isFinite"
  "built-ins/Number/isInteger"
  "built-ins/Number/isNaN"
  "built-ins/Number/parseFloat"
  "built-ins/Number/parseInt"
  "built-ins/Number/prototype"
  "built-ins/Object/assign"
  "built-ins/Object/create"
  "built-ins/Object/keys"
  "built-ins/Object/values"
  "built-ins/Object/entries"
  "built-ins/Object/is"
  "built-ins/Object/defineProperty"
  "built-ins/String/fromCharCode"
  "built-ins/String/fromCodePoint"
  "built-ins/String/raw"
  "built-ins/Math/abs"
  "built-ins/Math/floor"
  "built-ins/Math/max"
  "built-ins/Math/min"
  "built-ins/JSON/parse"
  "built-ins/JSON/stringify"
  "built-ins/decodeURI"
  "built-ins/encodeURI"
  "built-ins/isNaN"
  "built-ins/isFinite"
  "built-ins/parseFloat"
  "built-ins/parseInt"
  "built-ins/Symbol/for"
  "built-ins/Symbol/iterator"
  "built-ins/Symbol/keyFor"
  "built-ins/Symbol/toStringTag"
  "built-ins/Error/prototype"
  "built-ins/Function/prototype"
  "built-ins/globalThis"
)

for p in "${patterns[@]}"; do
  node -e "
  const fs=require('fs');
  const cfg=JSON.parse(fs.readFileSync('$CONFIG','utf8'));
  cfg.patterns=['$p'];
  fs.writeFileSync('$CONFIG', JSON.stringify(cfg, null, 2));
  "
  out=$(node "$RUNNER" 2>&1)
  snap=$(echo "$out" | grep -o 'tests/test262/reports/snapshot-[^ ]*\.json' | tail -1)
  if [ -z "$snap" ] || [ ! -f "$REPO/$snap" ]; then echo "No snapshot for $p"; exit 1; fi
  read -r total passed fs fm to <<< $(node -e "const s=require('./$snap'); console.log(s.total, s.summary.passed, s.summary.failed_syntax, s.summary.failed_semantics, s.summary.timeout)")
  node "$ADD_ROW" "$p" "$total" "$passed" "$fs" "$fm" "$to"
  git add CONFORMANCE_JS.md "$CONFIG" "$snap"
  git commit -m "docs(test262): record $p conformance"
  echo "Done: $p ($total tests)"
done
echo "All batches done."
