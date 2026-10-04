// GC safety: an exception thrown by a callee outlives the callee's context.
//
// A JavaScript call runs in a child ProtoContext; the objects the callee
// allocates are kept alive by that context's young generation until the call
// returns. ~ProtoContext anchors the call's return value in the caller's
// context, but an exception used to travel back only through C++ locals
// (childEx, pending_exception) and the interpreter's thread-local
// t_callException, none of which the collector can see. As long as nothing
// allocated before the exception reached an operand-stack slot this went
// unnoticed. A native that runs more JavaScript while it holds the exception
// -- IteratorClose calls the iterator's return() method and then rethrows the
// original exception -- allocates in that window, and the catch block received
// freed cells: `e.tag` read back as another object's string, or undefined.
//
// Each round throws a freshly allocated object through such a window and checks
// it in full in the catch block.
//
// Run by tests/cli/gc-stress.sh under PROTOCORE_HEAP_LIMIT_CELLS; also passes
// standalone (and under Node.js). Prints "ok exception_across_contexts" or the
// failures and exits 1.

var failures = 0;
function fail(m) { if (failures++ < 10) console.log('FAIL ' + m); }

function churn(r) {
    var g = 0;
    for (var i = 0; i < 20000; i++) { var o = { s: 'garbage-' + i + '-' + r, k: [i, i + 1] }; g += o.s.length; }
    return g;
}

// An iterator whose return() allocates enough for a collection.
function closingIterator(r) {
    return {
        [Symbol.iterator]() { return this; },
        next() { return { value: [1, 2], done: false }; },
        return() { churn(r); return {}; }
    };
}

function thrown(r) { return { tag: 'boom-' + r, list: ['a' + r, 'b' + r, 'c' + r] }; }

function check(e, r, label) {
    if (!e || e.tag !== 'boom-' + r || !e.list || e.list.length !== 3 || e.list[2] !== 'c' + r)
        fail(label + ' round ' + r + ': caught ' + (e && e.tag) + ' / ' + (e && e.list && e.list[2]));
}

for (var r = 0; r < 20; r++) {
    // Array.from: the mapping function throws; IteratorClose runs return().
    try { Array.from(closingIterator(r), function () { throw thrown(r); }); fail('Array.from did not throw'); }
    catch (e) { check(e, r, 'Array.from'); }

    // new Map / new Set: the adder throws; IteratorClose runs return().
    var savedSet = Map.prototype.set;
    Map.prototype.set = function () { throw thrown(r); };
    try { new Map(closingIterator(r)); fail('new Map did not throw'); }
    catch (e) { check(e, r, 'new Map'); }
    finally { Map.prototype.set = savedSet; }

    var savedAdd = Set.prototype.add;
    Set.prototype.add = function () { throw thrown(r); };
    try { new Set(closingIterator(r)); fail('new Set did not throw'); }
    catch (e) { check(e, r, 'new Set'); }
    finally { Set.prototype.add = savedAdd; }
}

if (failures) { console.log('FAIL exception_across_contexts: ' + failures); process.exit(1); }
console.log('ok exception_across_contexts');
