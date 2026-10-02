#!/usr/bin/env bash
#
# CLI check: child.kill(0) asks whether the child exists; it does not stop it.
#
# Signal 0 is the existence check of kill(2), and Node's subprocess.kill(0)
# returns true for a running child without touching it. Windows has no signals,
# and protoJS used to terminate the child there for every signal, 0 included.
# child.kill() also returned undefined on every platform; it now returns
# whether the signal was delivered, as Node's does.
#
# The child runs for about 1.5 s and then writes a marker file. The parent
# calls kill(0) twice while it runs, expects true both times, and then waits
# for the marker: a child that kill(0) terminated never writes it.
#
# On Windows the parent also checks that kill(0) answers false once the child
# has exited (the process handle is kept until then, so the pid cannot be
# reused under it). On Linux and macOS an exited child that nobody has waited
# for is a zombie and still exists for kill(2); protoJS does not reap children,
# so that half is Windows-only.
#
# Usage: child-kill-signal0.sh <path-to-protojs> <scratch-dir>
set -u

PROTOJS="${1:?usage: child-kill-signal0.sh <protojs> <scratch-dir>}"
SCRATCH="${2:?usage: child-kill-signal0.sh <protojs> <scratch-dir>}"

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH" || exit 1
cd "$SCRATCH" || exit 1

IS_WINDOWS=0
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) IS_WINDOWS=1 ;;
esac

# Paths handed to protojs on Windows must be Windows paths.
if [ "$IS_WINDOWS" -eq 1 ]; then
    BIN=$(cygpath -m "$PROTOJS")
    MARKER=$(cygpath -m "$SCRATCH")/marker.txt
else
    BIN="$PROTOJS"
    MARKER="$SCRATCH/marker.txt"
fi

cat > parent.js <<'EOF'
var cp = require('child_process');
var fs = require('fs');
var bin = process.env.KILL0_BIN;
var marker = process.env.KILL0_MARKER;
var windows = process.env.KILL0_WINDOWS === '1';

function fail(msg) { console.log('FAIL: ' + msg); throw new Error(msg); }
function exists(p) { try { fs.statSync(p); return true; } catch (e) { return false; } }

var child = cp.spawn(bin, ['-e',
    "var t = Date.now(); while (Date.now() - t < 1500) {} " +
    "require('fs').writeFileSync(process.env.KILL0_MARKER, 'done');"]);
if (!child || !(child.pid > 0)) fail('spawn did not start the child');

var r1 = child.kill(0);
var r2 = child.kill(0);
if (r1 !== true || r2 !== true)
    fail('kill(0) on a running child returned ' + r1 + ', ' + r2 + '; expected true, true');

var start = Date.now();
while (!exists(marker) && Date.now() - start < 30000) {}
if (!exists(marker)) fail('the child never finished: kill(0) stopped it');

if (windows) {
    var gone = false;
    start = Date.now();
    while (Date.now() - start < 30000) {
        if (child.kill(0) === false) { gone = true; break; }
    }
    if (!gone) fail('kill(0) still reports the child 30 s after it exited');
}
console.log('PASS: kill(0) checked the child without stopping it');
EOF

KILL0_BIN="$BIN" KILL0_MARKER="$MARKER" KILL0_WINDOWS="$IS_WINDOWS" \
    timeout 90 "$PROTOJS" parent.js
STATUS=$?
if [ "$STATUS" -ne 0 ]; then
    echo "FAIL: parent exited with status $STATUS"
    exit 1
fi
