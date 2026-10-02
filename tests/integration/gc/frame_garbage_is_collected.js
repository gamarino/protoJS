// The collector must reclaim what a long-running frame discards.
//
// Several interpreter handlers dispatch to the next opcode from inside a scope
// holding a ProtoContext::CriticalSection. The GCC build dispatched with a
// computed goto, which does not run the destructors of the scopes it leaves, so
// each such dispatch leaked one level of critical section. With the frame's
// depth above zero no safepoint handed its young generation to the collector:
// everything the frame allocated stayed a root until it returned. Writing one
// array element a million times exhausted any heap ceiling ("the last
// collections reclaimed nothing"); `x = [i]` in a loop overshot a 19 MB ceiling
// to 1.3 GB. Object property writes were unaffected, which is why the defect
// hid behind arrays, typed arrays and `new`.
//
// Run by tests/cli/gc-stress.sh under PROTOCORE_HEAP_LIMIT_CELLS=400000 (25 MB):
// each loop below allocates many times that. Prints "ok frame_garbage_is_collected".

var failures = 0;
function fail(msg) { if (failures++ < 5) console.log('FAIL ' + msg); }

var arr = [0, 0, 0];
for (var i = 0; i < 300000; i++) { arr[1] = i; }
if (arr[1] !== 299999) fail('array slot = ' + arr[1]);

var x = null;
for (var i = 0; i < 100000; i++) { x = [i, i + 1]; }
if (x[1] !== 100000) fail('last literal = ' + x);

var t = new Int32Array(3);
for (var i = 0; i < 300000; i++) { t[1] = i; }
if (t[1] !== 299999) fail('typed array slot = ' + t[1]);

var made = 0;
for (var i = 0; i < 50000; i++) { var m = new Map(); m.set(i, i); made += m.size; }
if (made !== 50000) fail('maps made = ' + made);

var total = 0;
for (var r = 0; r < 500; r++) { var a = []; for (var j = 0; j < 100; j++) a.push(j); total += a.length; }
if (total !== 50000) fail('pushed = ' + total);

if (failures) { console.log('FAIL frame_garbage_is_collected: ' + failures); process.exit(1); }
console.log('ok frame_garbage_is_collected');
