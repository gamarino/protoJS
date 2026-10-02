// GC safety: symbols reachable only through protoJS's own registries.
//
// A symbol used as a property key is stored under an internal string key; the
// object references that string, not the Symbol value. Object.getOwnPropertySymbols
// and Reflect.ownKeys translate the string back through a registry, and
// Symbol.for keeps its own registry. Neither registry was seen by the collector,
// so a symbol whose only holder was the registry was collected, and reading it
// back returned a dangling pointer (wrong identity or a crash).
//
// Run by tests/cli/gc-stale-cache-stress.sh under PROTOCORE_HEAP_LIMIT_CELLS;
// also passes standalone. Prints "ok stale_symbol_registry" or the failures and
// exits 1.

var failures = 0, checked = 0;
function fail(msg) { if (failures++ < 5) console.log('FAIL ' + msg); }

var holders = [];
for (var i = 0; i < 40; i++) {
    var h = {};
    h[Symbol('s' + i)] = i;            // the Symbol value is dropped at once
    holders.push(h);
}
var forKeys = [];
for (var i = 0; i < 40; i++) { Symbol.for('registered-' + i); }

for (var round = 0; round < 150; round++) {
    var garbage = 0;
    for (var i = 0; i < 500; i++) { var g = { k: 'garbage-string-' + i + '-' + round }; garbage += g.k.length; }
    for (var i = 0; i < holders.length; i++) {
        checked++;
        var syms = Object.getOwnPropertySymbols(holders[i]);
        if (syms.length !== 1 || typeof syms[0] !== 'symbol') { fail('holder ' + i + ' lost its symbol key'); continue; }
        if (holders[i][syms[0]] !== i) fail('holder ' + i + '[sym] = ' + holders[i][syms[0]]);
        if (syms[0].description !== 's' + i) fail('holder ' + i + ' symbol description = ' + syms[0].description);
        if (Reflect.ownKeys(holders[i])[0] !== syms[0]) fail('Reflect.ownKeys disagrees for holder ' + i);
    }
    for (var i = 0; i < 40; i += 7) {
        var s = Symbol.for('registered-' + i);
        if (Symbol.keyFor(s) !== 'registered-' + i) fail('Symbol.keyFor = ' + Symbol.keyFor(s));
        if (s !== Symbol.for('registered-' + i)) fail('Symbol.for identity changed');
    }
}
if (failures) { console.log('FAIL stale_symbol_registry: ' + failures + ' of ' + checked); process.exit(1); }
console.log('ok stale_symbol_registry');
