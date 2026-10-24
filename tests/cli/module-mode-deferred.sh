#!/usr/bin/env bash
#
# CLI check: a Deferred created by an ES module (--input-type=module, which
# QuickJS runs) runs on the DeferredPool of the main wrapper, in the process's
# one ProtoSpace, and settles its QuickJS `then` callback with the result.
#
# Until this check the module's Deferred built a JSContextWrapper -- and with it
# a whole ProtoSpace -- per task, and carried the result back through
# JS_WriteObject / JS_ReadObject.
#
# The module has no console (QuickJS's global), so the observable output is
# printed by the Deferred functions, which run in protoCore's realm: the first
# reports the number of live spaces, the then callback starts a second Deferred
# that prints what the first returned.
#
# Usage: module-mode-deferred.sh <path-to-protojs> <scratch-dir>
set -u

PROTOJS="${1:?usage: module-mode-deferred.sh <protojs> <scratch-dir>}"
SCRATCH="${2:?usage: module-mode-deferred.sh <protojs> <scratch-dir>}"

rm -rf "$SCRATCH"
mkdir -p "$SCRATCH" || exit 1
cd "$SCRATCH" || exit 1

cat > deferred.mjs <<'JS'
const d = new Deferred(() => {
    console.log('pool: spaces=' + protoCore.gcStats().liveSpaces);
    return 6 * 7;
});
d.then((v) => {
    new Deferred(new Function('console.log("then: ' + v + '")'));
});
JS

timeout 60 "$PROTOJS" --input-type=module deferred.mjs > out.txt 2> err.txt
status=$?
expected=$'pool: spaces=1\nthen: 42'
if [ "$status" -ne 0 ]; then
    echo "FAIL: exit status $status"; cat err.txt; exit 1
fi
if [ "$(cat out.txt)" != "$expected" ]; then
    echo "FAIL: standard output was:"; cat out.txt; echo "--- expected:"; echo "$expected"
    echo "--- standard error:"; cat err.txt
    exit 1
fi
echo "PASS: a module's Deferred runs in the one space and settles its then callback"
