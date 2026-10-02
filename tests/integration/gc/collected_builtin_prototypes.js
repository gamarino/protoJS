// GC safety: built-in prototypes made on first use and cached in a C++ static.
//
// %ArrayIteratorPrototype%, %SetIteratorPrototype% and the other iterator
// prototypes, the Deferred prototype and the prototypes of native-module
// objects (crypto hashes, sockets, ...) are made the first time one is needed
// and cached in a static. A static is not a root and nothing else references
// such a prototype, so once every object made from it had been collected the
// prototype was collected too, and the next object was built on the freed
// cell: `new Set([1]).values()` came back without its prototype's `next`, or
// the process crashed. Each round below makes one object of every kind, drops
// it, and allocates enough garbage for a collection before the next round.
//
// Run by tests/cli/gc-stress.sh under PROTOCORE_HEAP_LIMIT_CELLS; also passes
// standalone. Prints "ok collected_builtin_prototypes" or the failures and exits 1.

var failures = 0;
function fail(m) { if (failures++ < 5) console.log('FAIL ' + m); }
var crypto = require('crypto');

function churn(r) {
    var g = 0;
    for (var i = 0; i < 30000; i++) { var o = { s: 'garbage-' + i + '-' + r }; g += o.s.length; }
    return g;
}

function probe(r) {
    var iterators = [
        [new Set([1]).values(), 'Set Iterator'],
        [new Map([[1, 1]]).entries(), 'Map Iterator'],
        [[1][Symbol.iterator](), 'Array Iterator'],
        ['a'[Symbol.iterator](), 'String Iterator'],
        ['a'.matchAll(/a/g), 'RegExp String Iterator'],
    ];
    for (var i = 0; i < iterators.length; i++) {
        var it = iterators[i][0], tag = iterators[i][1];
        var p = Object.getPrototypeOf(it);
        if (!p || p[Symbol.toStringTag] !== tag)
            fail('round ' + r + ': ' + tag + ' prototype tag = ' + (p && p[Symbol.toStringTag]));
        else if (typeof it.next !== 'function') fail('round ' + r + ': ' + tag + ' lost next()');
        else {
            var n = it.next();
            if (!n || n.done !== false) fail('round ' + r + ': ' + tag + ' next() = ' + JSON.stringify(n));
        }
    }
    var h = crypto.createHash('sha256');
    if (typeof h.update !== 'function' || typeof h.digest !== 'function') fail('round ' + r + ': hash object lost its methods');
    else if (h.update('abc').digest('hex') !== 'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad')
        fail('round ' + r + ': sha256 digest wrong');
}

for (var r = 0; r < 15; r++) { probe(r); churn(r); }
if (failures) { console.log('FAIL collected_builtin_prototypes: ' + failures); process.exit(1); }
console.log('ok collected_builtin_prototypes');
