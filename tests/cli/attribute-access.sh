#!/usr/bin/env bash
# Runs attribute_access.js and compares its output with Node's
# (attribute_access.expected). Usage: attribute-access.sh <protojs>
set -u
here="$(cd "$(dirname "$0")" && pwd)"
actual="$("$1" "$here/attribute_access.js" 2>&1)"
if [ "$actual" != "$(cat "$here/attribute_access.expected")" ]; then
    echo "attribute_access.js output differs from Node's:"
    diff <(printf '%s\n' "$actual") "$here/attribute_access.expected"
    exit 1
fi
echo "attribute_access: output matches Node"
