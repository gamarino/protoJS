#!/usr/bin/env bash
#
# CLI check: an exception that crosses a module boundary is the exception that
# was thrown, caught by the caller's try statement or, uncaught, reported with
# a non-zero exit status.
#
# The interpreter names a function by its index in its module's function
# table. A closure created by the main script did not record its table, so a
# required module that called it resolved the index against its OWN table and
# ran an unrelated function: the exception was lost, or replaced by another
# ("TypeError: is not a function" when the index was out of range there).
# tests/integration/modules/test_cross_module_exception.js covers the caught
# cases for CommonJS; this script covers the uncaught case, which only the
# process's exit status and standard error show, and ES modules.
#
# Cases:
#   cjs-uncaught  a callback of the main script throws inside a required
#                 module; nothing catches it: exit status 1, and standard error
#                 names the error that was thrown
#   cjs-caught    the same exception, caught around the call into the module
#   esm-caught    an imported module's function catches what a callback of the
#                 importing module throws, and the importer catches what the
#                 imported module throws
#
# Usage: cross-module-exceptions.sh <path-to-protojs> <scratch-dir>
set -u

PROTOJS="${1:?usage: cross-module-exceptions.sh <protojs> <scratch-dir>}"
SCRATCH="${2:?usage: cross-module-exceptions.sh <protojs> <scratch-dir>}"

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH" || exit 1
cd "$SCRATCH" || exit 1

FAILED=0
fail() {
    echo "FAIL [$1]: $2"
    [ -f "$1.out" ] && cat "$1.out"
    FAILED=1
}

# The module has more functions than the main script has before its callback,
# so the callback's index is one the module's own table also has.
cat > through.js <<'EOF2'
function a() { return 'a'; }
function b() { return 'b'; }
function c() { return 'c'; }
module.exports = { a: a, b: b, c: c, call: function (fn) { return fn(); } };
EOF2

cat > cjs-uncaught.js <<'EOF2'
function cb() { throw new RangeError('thrown by the callback'); }
require('./through.js').call(cb);
console.log('NOT REACHED');
EOF2
timeout 60 "$PROTOJS" cjs-uncaught.js > cjs-uncaught.out 2>&1
status=$?
if [ "$status" -ne 1 ]; then
    fail cjs-uncaught "exit status $status, expected 1"
elif ! grep -q "RangeError: thrown by the callback" cjs-uncaught.out; then
    fail cjs-uncaught "the report does not name the thrown error"
elif grep -q "NOT REACHED" cjs-uncaught.out; then
    fail cjs-uncaught "execution continued after the uncaught exception"
else
    echo "ok   [cjs-uncaught]"
fi

cat > cjs-caught.js <<'EOF2'
function cb() { throw new RangeError('thrown by the callback'); }
let caught = null;
try {
    require('./through.js').call(cb);
} catch (e) {
    caught = e;
}
if (!(caught instanceof RangeError) || caught.message !== 'thrown by the callback')
    throw new Error('caught ' + caught);
EOF2
if timeout 60 "$PROTOJS" cjs-caught.js > cjs-caught.out 2>&1; then
    echo "ok   [cjs-caught]"
else
    fail cjs-caught "exit status $?"
fi

cat > lib.mjs <<'EOF2'
export function a() { return 'a'; }
export function b() { return 'b'; }
export function catchFrom(fn) { try { fn(); } catch (e) { return e; } return 'no exception'; }
export function thrower() { throw new TypeError('thrown by the module'); }
EOF2
cat > esm-caught.mjs <<'EOF2'
import { catchFrom, thrower } from './lib.mjs';
const r = catchFrom(function () { throw new RangeError('thrown by the callback'); });
if (!(r instanceof RangeError) || r.message !== 'thrown by the callback')
    throw new Error('the module caught ' + r);
let t = null;
try { thrower(); } catch (e) { t = e; }
if (!(t instanceof TypeError) || t.message !== 'thrown by the module')
    throw new Error('the importer caught ' + t);
EOF2
if timeout 60 "$PROTOJS" --input-type=module esm-caught.mjs > esm-caught.out 2>&1; then
    echo "ok   [esm-caught]"
else
    fail esm-caught "exit status $?"
fi

if [ "$FAILED" -ne 0 ]; then
    exit 1
fi
echo "PASS: exceptions cross module boundaries intact"
