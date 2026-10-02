// GC safety: the per-object behaviour cache.
//
// BehaviorRegistry caches, per object address, the behaviour that decides how
// writes land (ordinary, frozen, non-extensible, typed-array element). Once a
// frozen object was collected and its cell reused, a fresh ordinary object at
// the same address inherited the frozen behaviour and silently dropped writes.
//
// Run by tests/cli/gc-stale-cache-stress.sh under PROTOCORE_HEAP_LIMIT_CELLS;
// also passes standalone. Prints "ok stale_behavior_cache" or the failures and
// exits 1.

var failures = 0, checked = 0;
function fail(msg) { if (failures++ < 5) console.log('FAIL ' + msg); }

var ta = new Uint8Array(2);
var sink = 0;
for (var round = 0; round < 300; round++) {
    // Writes to frozen and non-extensible objects resolve (and cache) their
    // behaviour; the objects are dropped at once.
    for (var i = 0; i < 100; i++) {
        var f = Object.freeze({ a: 1 });
        f.a = 2;
        var s = Object.preventExtensions({ b: 1 });
        s.c = 3;
    }
    ta[0] = 300;                       // caches the typed-array behaviour
    // Garbage that makes no property writes, so the cache keeps the dead
    // objects' entries while the collector frees their cells.
    for (var i = 0; i < 2000; i++) sink += ('garbage-' + i + '-' + round).length;
    // Fresh ordinary objects, some at the freed addresses.
    for (var i = 0; i < 200; i++) {
        var o = { a: 1 };
        o.a = 7; o.z = 9;
        checked++;
        if (o.a !== 7 || o.z !== 9) fail('write to a fresh object dropped: a=' + o.a + ' z=' + o.z);
    }
}
if (failures) { console.log('FAIL stale_behavior_cache: ' + failures + ' of ' + checked); process.exit(1); }
console.log('ok stale_behavior_cache');
