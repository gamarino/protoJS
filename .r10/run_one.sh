#!/bin/bash
# Run a single test262 file with protojs (proto-eval path) and capture stderr
TEST_FILE="$1"
TEST262_ROOT="${TEST262_ROOT:-/home/gamarino/Documentos/proyectos/test262}"
PROTOJS="${PROTOJS:-/home/gamarino/Documentos/proyectos/protoJS/build_release/protojs}"

# Build a wrapper with assert + sta + includes
INCLUDES=$(grep -A 5 'includes:' "$TEST262_ROOT/test/$TEST_FILE" | head -20 | grep -oE '[A-Za-z0-9_.\-]+\.js' | sort -u || true)
WRAP=$(mktemp /tmp/proto262.XXXXXX.js)

cat "$TEST262_ROOT/harness/assert.js" "$TEST262_ROOT/harness/sta.js" > "$WRAP"
for inc in $INCLUDES; do
  if [ -f "$TEST262_ROOT/harness/$inc" ]; then
    cat "$TEST262_ROOT/harness/$inc" >> "$WRAP"
  fi
done
cat "$TEST262_ROOT/test/$TEST_FILE" >> "$WRAP"

PROTOJS_USE_PROTO_EVAL=1 timeout 8 "$PROTOJS" "$WRAP" 2>&1
RC=$?
rm -f "$WRAP"
exit $RC
