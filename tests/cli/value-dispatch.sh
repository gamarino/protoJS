#!/usr/bin/env bash
# Runs value_dispatch.js and compares its output with Node's
# (value_dispatch.expected). Usage: value-dispatch.sh <protojs>
set -u
here="$(cd "$(dirname "$0")" && pwd)"
actual="$("$1" "$here/value_dispatch.js" 2>&1)"
if [ "$actual" != "$(cat "$here/value_dispatch.expected")" ]; then
    echo "value_dispatch.js output differs from Node's:"
    diff <(printf '%s\n' "$actual") "$here/value_dispatch.expected"
    exit 1
fi
echo "value_dispatch: output matches Node"
