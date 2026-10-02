#!/usr/bin/env bash
#
# CLI check: process.exit(code) ends the process with that code, cleanly,
# wherever it is called -- at top level or from an event callback -- after
# everything the program wrote has reached its destination.
#
# process.exit called exit(), which runs the C++ static destructors while the
# script is still on the stack and the runtime's threads are still running: the
# I/O thread pool's destructor shut the pool down against a space it could no
# longer use, and the process died with SIGSEGV instead of the requested code.
# It now flushes the standard streams, runs the process's exit hooks (on
# Windows, the console code pages are restored) and ends the process at once.
#
# Every case writes a line, calls process.exit(N) and writes another line that
# must not appear. Standard output is a file, so the first line is only there if
# it was flushed. Cases: top-level, setImmediate, a promise reaction, an I/O
# completion (fs.promises.readFile), a worker's 'exit' handler, and exit(0)
# with work still pending (the pending work must not run).
#
# Usage: process-exit.sh <path-to-protojs> <scratch-dir>
set -u

PROTOJS="${1:?usage: process-exit.sh <protojs> <scratch-dir>}"
SCRATCH="${2:?usage: process-exit.sh <protojs> <scratch-dir>}"

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH" || exit 1
cd "$SCRATCH" || exit 1

FAILED=0

# run_case <name> <expected-status> : runs <name>.js; its output must be "<name>: before".
run_case() {
    local name="$1" expected="$2"
    timeout 60 "$PROTOJS" "$name.js" > "$name.out" 2> "$name.err"
    local status=$?
    local out
    out="$(cat "$name.out")"
    if [ "$status" -ne "$expected" ]; then
        echo "FAIL [$name]: exit status $status, expected $expected"
        cat "$name.out" "$name.err"
        FAILED=1
    elif [ "$out" != "$name: before" ]; then
        echo "FAIL [$name]: standard output was:"
        cat "$name.out"
        FAILED=1
    else
        echo "ok   [$name]"
    fi
}

cat > top-level.js <<'EOF2'
console.log('top-level: before');
process.exit(3);
console.log('top-level: after');
EOF2
run_case top-level 3

cat > immediate.js <<'EOF2'
setImmediate(function () {
    console.log('immediate: before');
    process.exit(4);
    console.log('immediate: after');
});
EOF2
run_case immediate 4

cat > promise-reaction.js <<'EOF2'
Promise.resolve().then(function () {
    console.log('promise-reaction: before');
    process.exit(5);
    console.log('promise-reaction: after');
});
EOF2
run_case promise-reaction 5

cat > io-completion.js <<'EOF2'
require('fs').promises.readFile(__filename).then(function (data) {
    if (data.length > 0) console.log('io-completion: before');
    process.exit(6);
    console.log('io-completion: after');
});
EOF2
run_case io-completion 6

cat > worker-child.js <<'EOF2'
parentPort.postMessage('done');
EOF2
cat > worker-exit.js <<'EOF2'
var wt = require('worker_threads');
var w = new wt.Worker(__dirname + '/worker-child.js');
w.on('exit', function () {
    console.log('worker-exit: before');
    process.exit(7);
    console.log('worker-exit: after');
});
EOF2
run_case worker-exit 7

cat > pending-work.js <<'EOF2'
setImmediate(function () { console.log('pending-work: after'); });
Promise.resolve().then(function () {
    console.log('pending-work: before');
    process.exit(0);
});
EOF2
run_case pending-work 0

if [ "$FAILED" -ne 0 ]; then
    exit 1
fi
echo "PASS: process.exit ends the process cleanly with the requested code"
