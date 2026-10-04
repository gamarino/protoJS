// GC safety: the string "undefined" that ToString(undefined) returns.
//
// The interpreter cached the result of ToString(undefined) in a C++ static the
// first time it was needed. "undefined" is longer than protoCore's inline
// strings (6 bytes), so it was an ordinary heap cell, and a static is not a
// root: once the collector freed it, `'' + undefined` and `String(undefined)`
// returned whatever string later occupied the cell ("", or another string).
// The string is now kept per space and pinned (PinnedBuiltin).
//
// Run by tests/cli/gc-stress.sh under PROTOCORE_HEAP_LIMIT_CELLS; also passes
// standalone. Prints "ok cached_undefined_string" or the failures and exits 1.

var failures = 0;
function fail(m) { if (failures++ < 5) console.log('FAIL ' + m); }

function churn(r) {
    var g = 0;
    for (var i = 0; i < 30000; i++) { var s = 'undefinee' + (i % 7) + r; var o = { s: s, t: s + 'x' }; g += o.t.length; }
    return g;
}

function probe(r) {
    var u;
    var s = '' + u;
    if (s !== 'undefined') fail('round ' + r + ": '' + undefined = " + JSON.stringify(s));
    var t = String(u) + '!';
    if (t !== 'undefined!') fail('round ' + r + ': String(undefined) + "!" = ' + JSON.stringify(t));
    if ([u].join() !== '' || `${u}` !== 'undefined') fail('round ' + r + ': template literal ' + JSON.stringify(`${u}`));
}

for (var r = 0; r < 20; r++) { probe(r); churn(r); }
if (failures) { console.log('FAIL cached_undefined_string: ' + failures); process.exit(1); }
console.log('ok cached_undefined_string');
