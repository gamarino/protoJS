#!/usr/bin/env bash
#
# Compares every promise fixture's .expected file with what Node prints for it
# (tests/integration/promises). The .expected files are Node's output, and this
# is how that claim is checked: run it after changing a fixture, with
# --update to rewrite the .expected files from Node.
#
# Usage: check-promise-fixtures-against-node.sh [--update]
set -u
DIR="$(cd "$(dirname "$0")/../integration/promises" && pwd)"
UPDATE=0
[ "${1:-}" = "--update" ] && UPDATE=1
command -v node > /dev/null || { echo "node not found"; exit 2; }
echo "node $(node --version)"
FAILED=0
for expected in "$DIR"/*.expected; do
    name="$(basename "$expected" .expected)"
    actual="$(cd "$DIR" && node "$name.js" 2>&1)"
    if [ "$UPDATE" -eq 1 ]; then
        printf '%s\n' "$actual" > "$expected"
        echo "updated [$name]"
    elif [ "$actual" != "$(cat "$expected")" ]; then
        echo "DIFFERS [$name]"
        diff <(printf '%s\n' "$actual") "$expected" | sed 's/^/    /'
        FAILED=1
    else
        echo "same    [$name]"
    fi
done
exit $FAILED
