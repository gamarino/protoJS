#!/usr/bin/env bash
# Runs console_values.js and compares its output with Node's
# (console_values.expected). Usage: console-values.sh <protojs>
set -u
here="$(cd "$(dirname "$0")" && pwd)"
actual="$("$1" "$here/console_values.js" 2>&1)"
if [ "$actual" != "$(cat "$here/console_values.expected")" ]; then
    echo "console_values.js output differs from Node's:"
    diff <(printf '%s\n' "$actual") "$here/console_values.expected"
    exit 1
fi
echo "console_values: output matches Node"
