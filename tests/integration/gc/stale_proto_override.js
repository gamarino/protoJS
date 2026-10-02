// GC safety: a [[Prototype]] override must belong to its object, not to an
// address.
//
// Object.create(null) and Object.setPrototypeOf record a JS [[Prototype]] that
// protoCore's parent chain cannot express. That record was kept in a C++ map
// keyed by the object's address, which the collector does not see: once a
// recorded object was collected and its cell reused, a brand-new `{}` at the
// same address reported the dead object's prototype (null, or some other
// object). Under a small heap ceiling about a third of fresh objects did.
//
// Run by tests/cli/gc-stale-cache-stress.sh under PROTOCORE_HEAP_LIMIT_CELLS;
// also passes standalone. Prints "ok stale_proto_override" or the failures and
// exits 1.

var failures = 0, checked = 0;
var P = { tag: 'P' };
for (var round = 0; round < 200; round++) {
    for (var i = 0; i < 300; i++) { var d = Object.create(null); d.x = i; }
    for (var i = 0; i < 300; i++) { var s = { y: i }; Object.setPrototypeOf(s, P); }
    for (var i = 0; i < 300; i++) {
        var o = { a: i };
        checked++;
        if (Object.getPrototypeOf(o) !== Object.prototype
            || typeof o.hasOwnProperty !== 'function'
            || o.tag !== undefined) {
            if (failures++ < 5) console.log('FAIL fresh object has a foreign prototype (round ' + round + ')');
        }
    }
    // The overrides that are still live keep working.
    var keep = Object.create(null);
    var kept = Object.setPrototypeOf({}, P);
    if (Object.getPrototypeOf(keep) !== null || kept.tag !== 'P') {
        if (failures++ < 5) console.log('FAIL live override lost (round ' + round + ')');
    }
}
if (failures) { console.log('FAIL stale_proto_override: ' + failures + ' of ' + checked); process.exit(1); }
console.log('ok stale_proto_override');
