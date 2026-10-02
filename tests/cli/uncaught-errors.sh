#!/usr/bin/env bash
#
# CLI check: an exception nobody catches ends the process with status 1, as in
# Node, wherever it is thrown -- in the main script, in an event-loop callback
# (setImmediate, an fs callback, a Deferred reaction) -- and so does a promise
# rejection that no handler has claimed by the end of the turn that produced it
# (Node's default since v15, --unhandled-rejections=throw).
#
# Before the fix, an exception thrown in an event-loop callback was printed (or,
# for setImmediate, silently dropped) and the program went on and exited with
# status 0, and an unhandled rejection was not reported at all: a failing
# program looked like a passing one to every caller that checks the status.
#
# Each case states its expected status, a text standard error must contain (for
# the failing cases, the error's message) and the exact standard output. After an
# uncaught error the process ends at once: work still queued does not run, so a
# line printed by queued work must not appear.
#
# Usage: uncaught-errors.sh <path-to-protojs> <scratch-dir>
set -u

PROTOJS="${1:?usage: uncaught-errors.sh <protojs> <scratch-dir>}"
SCRATCH="${2:?usage: uncaught-errors.sh <protojs> <scratch-dir>}"

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH" || exit 1
cd "$SCRATCH" || exit 1

FAILED=0

# run_case <name> <expected-status> <stderr-substring> <expected-stdout>
# Runs <name>.js. An empty <stderr-substring> means standard error must be empty.
run_case() {
    local name="$1" expected="$2" errtext="$3" expout="$4"
    timeout 60 "$PROTOJS" "$name.js" > "$name.out" 2> "$name.err"
    local status=$?
    local out
    out="$(cat "$name.out")"
    if [ "$status" -ne "$expected" ]; then
        echo "FAIL [$name]: exit status $status, expected $expected"
        echo "  stdout:"; sed 's/^/    /' "$name.out"
        echo "  stderr:"; sed 's/^/    /' "$name.err"
        FAILED=1
    elif [ "$out" != "$expout" ]; then
        echo "FAIL [$name]: standard output was:"
        sed 's/^/    /' "$name.out"
        echo "  expected:"; echo "$expout" | sed 's/^/    /'
        FAILED=1
    elif [ -n "$errtext" ] && ! grep -qF -- "$errtext" "$name.err"; then
        echo "FAIL [$name]: standard error does not contain '$errtext':"
        sed 's/^/    /' "$name.err"
        FAILED=1
    elif [ -z "$errtext" ] && [ -s "$name.err" ]; then
        echo "FAIL [$name]: unexpected standard error:"
        sed 's/^/    /' "$name.err"
        FAILED=1
    else
        echo "ok   [$name]"
    fi
}

# ---- Exceptions thrown in event-loop callbacks -------------------------------

cat > immediate-throw.js <<'EOF'
setImmediate(function () {
    console.log('immediate: before');
    throw new Error('boom-immediate');
});
setImmediate(function () { console.log('immediate: queued work ran'); });
EOF
run_case immediate-throw 1 "boom-immediate" "immediate: before"

cat > fs-callback-throw.js <<'EOF'
require('fs').readFile(__filename, function (err, data) {
    console.log('fs-callback: before');
    throw new TypeError('boom-fs-callback');
});
EOF
run_case fs-callback-throw 1 "boom-fs-callback" "fs-callback: before"

cat > deferred-reaction-throw.js <<'EOF'
require('fs').promises.readFile(__filename).then(function () {
    console.log('deferred: before');
    throw new RangeError('boom-deferred');
});
EOF
run_case deferred-reaction-throw 1 "boom-deferred" "deferred: before"

cat > main-throw.js <<'EOF'
console.log('main: before');
setImmediate(function () { console.log('main: queued work ran'); });
throw new Error('boom-main');
EOF
run_case main-throw 1 "boom-main" "main: before"

# ---- Unhandled promise rejections ------------------------------------------

cat > unhandled-rejection.js <<'EOF'
console.log('rejection: before');
Promise.reject(new Error('boom-rejection'));
EOF
run_case unhandled-rejection 1 "boom-rejection" "rejection: before"

cat > unhandled-executor-reject.js <<'EOF'
new Promise(function (resolve, reject) { reject(new Error('boom-executor')); });
EOF
run_case unhandled-executor-reject 1 "boom-executor" ""

cat > unhandled-derived.js <<'EOF'
// then() without a rejection handler hands the rejection to a new promise,
// which nobody handles.
Promise.reject(new Error('boom-derived')).then(function () {});
EOF
run_case unhandled-derived 1 "boom-derived" ""

cat > unhandled-in-callback.js <<'EOF'
setImmediate(function () {
    console.log('callback-rejection: before');
    Promise.reject(new Error('boom-callback-rejection'));
});
EOF
run_case unhandled-in-callback 1 "boom-callback-rejection" "callback-rejection: before"

cat > unhandled-non-error.js <<'EOF'
Promise.reject(42);
EOF
run_case unhandled-non-error 1 "42" ""

cat > unhandled-async-throw.js <<'EOF'
// A throw after an await rejects the async function's promise; nobody handles
// it, so the process ends at the end of that job's checkpoint.
async function f() { await null; throw new Error('boom-async-await'); }
f();
setImmediate(function () { console.log('async: queued work ran'); });
EOF
run_case unhandled-async-throw 1 "boom-async-await" ""

cat > unhandled-async-before-await.js <<'EOF'
// A throw before the first await also rejects the promise: the caller goes on.
async function f() { throw new Error('boom-async-sync'); }
f();
console.log('after the call');
EOF
run_case unhandled-async-before-await 1 "boom-async-sync" "after the call"

cat > unhandled-handled-next-macrotask.js <<'EOF'
// A handler attached in a later macrotask is too late, as in Node: the
// rejection is reported at the end of the checkpoint that produced it.
var p = Promise.reject(new Error('boom-too-late'));
setImmediate(function () { p.catch(function () { console.log('handled too late'); }); });
console.log('end');
EOF
run_case unhandled-handled-next-macrotask 1 "boom-too-late" "end"

# ---- Handled rejections and clean programs end with status 0 -----------------

cat > handled-catch.js <<'EOF'
Promise.reject(new Error('x')).catch(function (e) { console.log('caught ' + e.message); });
EOF
run_case handled-catch 0 "" "caught x"

cat > handled-later-same-turn.js <<'EOF'
var p = Promise.reject(new Error('y'));
console.log('between');
p.then(null, function (e) { console.log('handled ' + e.message); });
EOF
run_case handled-later-same-turn 0 "" "between
handled y"

cat > handled-derived.js <<'EOF'
Promise.reject(new Error('z')).then(function () {}).catch(function (e) {
    console.log('derived handled ' + e.message);
});
EOF
run_case handled-derived 0 "" "derived handled z"

cat > handled-combinators.js <<'EOF'
Promise.all([Promise.reject(new Error('a'))]).catch(function (e) { console.log('all ' + e.message); });
Promise.allSettled([Promise.reject(new Error('b'))]).then(function (r) { console.log('allSettled ' + r[0].status); });
Promise.race([Promise.reject(new Error('c'))]).catch(function (e) { console.log('race ' + e.message); });
Promise.any([Promise.reject(new Error('d'))]).catch(function (e) { console.log('any ' + e.name); });
Promise.reject(new Error('e')).finally(function () {}).catch(function (e) { console.log('finally ' + e.message); });
EOF
run_case handled-combinators 0 "" "all a
allSettled rejected
race c
any AggregateError
finally e"

cat > handled-later-microtask.js <<'EOF'
// A handler attached by a later job of the same checkpoint is in time.
var p = Promise.reject(new Error('same-checkpoint'));
Promise.resolve().then(function () {}).then(function () {
    p.catch(function (e) { console.log('handled in a later job ' + e.message); });
});
EOF
run_case handled-later-microtask 0 "" "handled in a later job same-checkpoint"

cat > handled-await.js <<'EOF'
async function f() {
    try { await Promise.reject(new Error('w')); }
    catch (e) { console.log('await caught ' + e.message); }
}
f();
EOF
run_case handled-await 0 "" "await caught w"

cat > caught-in-callback.js <<'EOF'
setImmediate(function () {
    try { throw new Error('inner'); } catch (e) { console.log('caught ' + e.message); }
});
setImmediate(function () { console.log('second'); });
EOF
run_case caught-in-callback 0 "" "caught inner
second"

# A TypeError raised by a native store (strict-mode writes to a frozen object,
# by name, by Symbol and by index) and caught must leave nothing pending: the
# store paths cleared the exception but not the "a native call threw" flag, so
# the next native call (console.log) re-threw it -- silently swallowing output,
# and fatal at the end of the turn.
cat > caught-native-store.js <<'EOF2'
'use strict';
var sym = Symbol('s');
var obj = { a: 1, 0: 'x' };
obj[sym] = 1;
Object.freeze(obj);
try { obj[sym] = 2; } catch (e) { console.log('symbol ' + e.name); }
console.log('after symbol');
try { obj.a = 2; } catch (e) { console.log('name ' + e.name); }
console.log('after name');
try { obj[0] = 'y'; } catch (e) { console.log('index ' + e.name); }
console.log('after index');
setImmediate(function () {
    try { obj[sym] = 3; } catch (e) { console.log('callback ' + e.name); }
    console.log('after callback');
});
EOF2
run_case caught-native-store 0 "" "symbol TypeError
after symbol
name TypeError
after name
index TypeError
after index
callback TypeError
after callback"

if [ "$FAILED" -ne 0 ]; then
    exit 1
fi
echo "PASS: uncaught exceptions and unhandled rejections end the process with status 1"
