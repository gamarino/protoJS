#!/usr/bin/env bash
#
# CLI check: promise and async-function job ordering matches Node.
#
# Every <name>.js in the fixture directory that has a sibling <name>.expected is
# run, and its standard output must equal <name>.expected exactly, with exit
# status 0 and nothing on standard error. The .expected files are Node's own
# output (tests/scripts/check-promise-fixtures-against-node.sh regenerates and
# compares them), so a fixture states the ECMAScript job order -- which reaction
# runs in which job, how many jobs a thenable adoption or an await costs, and
# that the job queue is drained after the script and after every macrotask --
# not protoJS's idea of it.
#
# A fixture without an .expected file asserts its own results instead and must
# print "<name>: all checks passed" (used where Node 22 lacks the feature, such
# as Promise.try).
#
# Usage: promise-fixtures.sh <path-to-protojs> <fixture-dir>
set -u

PROTOJS="${1:?usage: promise-fixtures.sh <protojs> <fixture-dir>}"
DIR="${2:?usage: promise-fixtures.sh <protojs> <fixture-dir>}"

FAILED=0
COUNT=0
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

for script in "$DIR"/*.js; do
    name="$(basename "$script" .js)"
    COUNT=$((COUNT + 1))
    timeout 120 "$PROTOJS" "$script" > "$TMP/$name.out" 2> "$TMP/$name.err" < /dev/null
    status=$?
    reason=""
    if [ "$status" -ne 0 ]; then
        reason="exit status $status"
    elif [ -s "$TMP/$name.err" ]; then
        reason="unexpected standard error"
    elif [ -f "$DIR/$name.expected" ]; then
        if ! diff -u "$DIR/$name.expected" "$TMP/$name.out" > "$TMP/$name.diff"; then
            reason="output differs from Node's"
        fi
    elif ! grep -qxF "$name: all checks passed" "$TMP/$name.out"; then
        reason="did not print '$name: all checks passed'"
    fi
    if [ -n "$reason" ]; then
        FAILED=$((FAILED + 1))
        echo "FAIL [$name]: $reason"
        [ -s "$TMP/$name.diff" ] && sed 's/^/    /' "$TMP/$name.diff"
        [ -s "$TMP/$name.diff" ] || { echo "  stdout:"; sed 's/^/    /' "$TMP/$name.out"; }
        echo "  stderr:"; sed 's/^/    /' "$TMP/$name.err"
    else
        echo "ok   [$name]"
    fi
done

if [ "$FAILED" -ne 0 ]; then
    echo "FAIL: $FAILED of $COUNT promise fixtures"
    exit 1
fi
echo "PASS: $COUNT promise fixtures"
