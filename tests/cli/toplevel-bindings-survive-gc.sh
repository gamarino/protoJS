#!/usr/bin/env bash
#
# CLI check: top-level bindings survive a collection.
#
# THE DEFECT. A script's top-level bindings (`var`, `let`, `const`) do not live on
# the global object itself. The first time the root module runs, the interpreter
# splits the global: it creates a small mutable child of the global, the module
# scope, and from then on `*pGlobalRoot` -- for a script, the wrapper's
# nativeGlobalRoot_ -- points at that child, so every top-level write lands there
# and reads chain-walk to the global for the built-ins.
#
# The module scope's handle was referenced from nowhere the collector looks: not
# from a frame slot, not from the global (a parent does not reference its
# children), only from the C++ member nativeGlobalRoot_. It started as a young cell
# of the eval frame's context, which protects it only until that context's
# allocation threshold hands its young generation to the collector. The next cycle
# then found the handle unmarked, swept it, and released its entry in the mutables
# table: every top-level binding read back as undefined while the built-ins, which
# live on the global, kept working, and once the freed cell was reused the process
# crashed. On Linux master this failed about 3 runs in 30; on Windows and macOS
# every run.
#
# A top-level function declaration hides the defect -- the function object is a
# binding of the module scope and reaches the scope back through its parent chain,
# which puts the scope on a path from the mutables table -- so the first program
# below declares none.
#
# WHAT IS CHECKED.
#   inside   Three bindings are created, a top-level loop allocates enough under
#            the heap ceiling to force collection, and the bindings are read back
#            while the script is still running. This is the case that failed.
#   after    The same bindings are read from an event-loop callback, after the
#            script's eval has returned and its frame context is gone, with the
#            allocation done inside the callback. The module scope must stay
#            reachable for the life of the wrapper, not only of the frame that
#            created it. (The callback closure also reaches the scope while it is
#            pinned, so this case did not fail before the fix; it guards the fix
#            from being narrowed to the eval frame.)
# Each run must print the expected line exactly, exit 0 and report that collection
# really ran: a run that passes without a cycle exercised nothing and counts as a
# failed premise.
#
# The failure was intermittent on Linux, so one run proves little: `inside` runs
# RUNS times (default 50), `after` a fifth as many, and every run must pass.
#
# Usage: toplevel-bindings-survive-gc.sh <path-to-protojs> <scratch-dir> [runs]
set -u

PROTOJS="${1:?usage: toplevel-bindings-survive-gc.sh <protojs> <scratch-dir> [runs]}"
SCRATCH="${2:?usage: toplevel-bindings-survive-gc.sh <protojs> <scratch-dir> [runs]}"
RUNS="${3:-50}"

mkdir -p "$SCRATCH" || exit 1

LIMIT=500000   # cells; the 200k-iteration loops below must collect under it
DEADLINE=60    # seconds per run; a working run takes well under one

# ("garbage-" + i).length is 8 plus the digits of i, so 200,000 iterations give
# exactly 2,688,890.
cat > "$SCRATCH/inside.js" <<'EOF'
var v = {a: 1};
let l = {b: 2};
const c = 3;
let acc = 0;
for (let i = 0; i < 200000; i++) { acc += ("garbage-" + i).length; }
console.log('inside ' + [typeof v === 'object' ? v.a : typeof v,
                         typeof l === 'object' ? l.b : typeof l,
                         c].join(',') + ' ' + acc);
EOF

cat > "$SCRATCH/after.js" <<'EOF'
var v = {a: 1};
let l = {b: 2};
const c = 3;
setImmediate(function () {
  let acc = 0;
  for (let i = 0; i < 200000; i++) { acc += ("garbage-" + i).length; }
  console.log('after ' + [typeof v === 'object' ? v.a : typeof v,
                          typeof l === 'object' ? l.b : typeof l,
                          c].join(',') + ' ' + acc);
});
EOF

failures=0
total=0

# $1 program name (also the first word of its expected line), $2 runs
run_case() {
    local name="$1" runs="$2" expected="$1 1,2,3 2688890"
    local run out rc reason cycles
    for run in $(seq 1 "$runs"); do
        total=$((total + 1))
        out=$(timeout $DEADLINE env PROTOJS_GC_STATS=1 \
                  PROTOCORE_HEAP_LIMIT_CELLS=$LIMIT \
                  "$PROTOJS" "$SCRATCH/$name.js" 2>&1)
        rc=$?
        reason=""
        if [ $rc -ne 0 ]; then
            reason="exited $rc"
        elif ! printf '%s\n' "$out" | grep -qxF "$expected"; then
            reason="a top-level binding was lost (expected '$expected')"
        else
            cycles=$(printf '%s\n' "$out" | grep -F 'protojs gc:' | tail -1 \
                         | sed -n 's/.*cycles=\([0-9]*\).*/\1/p')
            if [ -z "$cycles" ] || [ "$cycles" -lt 1 ]; then
                reason="premise not met: no collection cycle ran (cycles='${cycles}')"
            fi
        fi
        if [ -n "$reason" ]; then
            failures=$((failures + 1))
            echo "FAIL [$name] run $run/$runs: $reason"
            printf '%s\n' "$out" | tail -3 | sed 's/^/      /'
        fi
    done
}

run_case inside "$RUNS"
run_case after $(( RUNS / 5 > 0 ? RUNS / 5 : 1 ))

if [ "$failures" -ne 0 ]; then
    echo "FAIL: $failures of $total runs failed under PROTOCORE_HEAP_LIMIT_CELLS=$LIMIT"
    exit 1
fi
echo "OK ($total runs)"
