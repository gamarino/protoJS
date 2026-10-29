// GC safety: a function's frame object outlives the closures made from it.
//
// The first closure a call creates that captures one of the call's locals
// makes the call's frame object (getOrCreateFrameObj in ProtoInterpreter.cpp):
// a protoCore object that becomes a parent of every closure the call creates,
// and of each closure's capture scope. The interpreter kept it only in the
// thread-local t_currentFrameObj, which the collector does not scan. Once the
// closures made so far had died and the call's young generation had been
// handed to the collector, nothing reachable referenced the frame object; a
// collection freed it, the cell was reused, and the next closure was built on
// the dead cell: its capture scope got a parent link that pointed at whatever
// the cell now held. The collector then aborted on the corrupt link
// ("CRITICAL TAGGED POINTER ... type 23"), or the process crashed. Found by the
// structure benchmark "records" in parallel mode, whose Deferreds turn on
// protoCore's adaptive heap, which collects often.
//
// Each call below creates thousands of short-lived closures that capture a
// local, with garbage between them so that collections run while the call is
// still creating closures, and checks every result.
//
// Run by tests/cli/gc-stress.sh under PROTOCORE_HEAP_LIMIT_CELLS; also passes
// standalone (and under Node.js). Prints "ok frame_object_outlives_closures" or
// the failures and exits 1.

var failures = 0;
function fail(m) { if (failures++ < 10) console.log('FAIL ' + m); }

function run(round) {
    var threshold = 1 + (round % 3);
    var kept = 0;
    var expected = 0;
    for (var i = 0; i < 4000; i++) {
        // Garbage: records like the benchmark's, dropped at once.
        var items = [];
        for (var j = 0; j < 5; j++) items.push({ sku: 'sku-' + i + '-' + j, qty: (i + j) % 10 });
        // A closure that captures the locals `threshold` and `round`.
        var lines = items.filter(function (it) { return it.qty >= threshold && round >= 0; });
        kept += lines.length;
        for (var j2 = 0; j2 < 5; j2++) if ((i + j2) % 10 >= threshold) expected++;
    }
    var byKey = new Map();
    for (var k = 0; k < 200; k++) byKey.set('k' + k, k * round);
    var sorted = Array.from(byKey.entries()).sort(function (a, b) { return b[1] - a[1] || (a[0] < b[0] ? -1 : 1); });
    if (kept !== expected) fail('round ' + round + ': kept ' + kept + ', expected ' + expected);
    if (sorted.length !== 200 || (round > 0 && sorted[0][1] !== 199 * round))
        fail('round ' + round + ': sort gave ' + sorted.length + ' entries, first ' + sorted[0]);
}

for (var r = 0; r < 30; r++) run(r);

if (failures === 0) console.log('ok frame_object_outlives_closures');
else { console.log(failures + ' failures'); if (typeof process !== 'undefined') process.exit(1); }
