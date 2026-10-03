#!/usr/bin/env bash
#
# CLI check: programs that churn memory under a small heap ceiling.
#
# protoJS sets a heap ceiling by default (75 % of memory), so protoCore's collector
# runs during ordinary programs, frees cells and reuses their addresses. Every
# script in tests/integration/gc/ exercises one place where that used to go
# wrong: a C++ cache keyed by a cell's address (the address is reused by a new
# cell, which then reads the dead cell's entry), a registry the collector did
# not see (it handed out freed cells), or a frame whose garbage was never
# reclaimed. Each script checks what it computes and prints "ok <name>".
#
# Under a small ceiling the collector runs many times per script. Whether a
# freed address is reused by the right kind of cell at the right moment is a
# matter of timing, so one passing run proves little: each script runs RUNS
# times, and every run must
#   - exit 0 and print "ok <script name>",
#   - report at least one collection cycle (PROTOJS_GC_STATS): a run in which
#     no collection happened exercised nothing and counts as a failed premise.
#
# Usage: gc-stress.sh <path-to-protojs> <runs> <heap-limit-cells> <script.js>...
set -u

PROTOJS="${1:?usage: gc-stress.sh <protojs> <runs> <heap-limit-cells> <script.js>...}"
RUNS="${2:?usage: gc-stress.sh <protojs> <runs> <heap-limit-cells> <script.js>...}"
LIMIT="${3:?usage: gc-stress.sh <protojs> <runs> <heap-limit-cells> <script.js>...}"
shift 3
[ "$#" -gt 0 ] || { echo "gc-stress.sh: no scripts given"; exit 2; }

DEADLINE=120   # seconds per run; the slowest script takes about ten
failures=0
total=0

for script in "$@"; do
    name=$(basename "$script" .js)
    for run in $(seq 1 "$RUNS"); do
        total=$((total + 1))
        out=$(timeout $DEADLINE env PROTOJS_GC_STATS=1 \
                  PROTOCORE_HEAP_LIMIT_CELLS="$LIMIT" \
                  "$PROTOJS" "$script" 2>&1 < /dev/null)
        rc=$?
        reason=""
        if [ $rc -ne 0 ]; then
            reason="exited $rc"
        elif ! printf '%s\n' "$out" | grep -qxF "ok $name"; then
            reason="did not print 'ok $name'"
        else
            cycles=$(printf '%s\n' "$out" | grep -F 'protojs gc:' | tail -1 \
                         | sed -n 's/.*cycles=\([0-9]*\).*/\1/p')
            if [ -z "$cycles" ] || [ "$cycles" -lt 1 ]; then
                reason="premise not met: no collection cycle ran (cycles='${cycles}')"
            fi
        fi
        if [ -n "$reason" ]; then
            failures=$((failures + 1))
            echo "FAIL [$name] run $run/$RUNS: $reason"
            printf '%s\n' "$out" | tail -4 | sed 's/^/      /'
        fi
    done
done

if [ "$failures" -ne 0 ]; then
    echo "FAIL: $failures of $total runs failed under PROTOCORE_HEAP_LIMIT_CELLS=$LIMIT"
    exit 1
fi
echo "OK ($total runs of $# scripts under PROTOCORE_HEAP_LIMIT_CELLS=$LIMIT)"
