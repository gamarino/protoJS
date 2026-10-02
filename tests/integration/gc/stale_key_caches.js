// GC safety: per-thread caches keyed by a string's address.
//
// Property keys computed at run time ('property' + i) are heap strings. The
// interpreter cached, per string address, the interned key it resolves to and
// whether it is a canonical array index. The collector does not see those
// caches, so once a key string was collected and its cell reused by a string
// with different content, the new string resolved to the dead one's key: a
// read of o['otherkey...'] returned o['property...'], and a numeric key was
// taken for a named one (or the reverse).
//
// The absent and non-index keys repeat over a bounded set of contents (each
// made as a fresh string every time) because every distinct key a program uses
// is interned for the life of the process.
//
// Run by tests/cli/gc-stale-cache-stress.sh under PROTOCORE_HEAP_LIMIT_CELLS;
// also passes standalone. Prints "ok stale_key_caches" or the failures and
// exits 1.

var failures = 0, checked = 0;
function fail(msg) { if (failures++ < 5) console.log('FAIL ' + msg); }

var o = {};
for (var i = 0; i < 64; i++) o['property' + i] = i;
var arr = [];
for (var i = 0; i < 64; i++) arr.push(i);

for (var round = 0; round < 400; round++) {
    for (var i = 0; i < 64; i++) {
        // Named keys longer than an inline string: heap strings, made fresh.
        var named = 'property' + i;
        checked++;
        if (o[named] !== i) fail('o[' + named + '] = ' + o[named]);
        var absent = 'unrelated' + ((round * 64 + i) % 1500);
        if (o[absent] !== undefined) fail('o[' + absent + '] = ' + o[absent] + ' (a stale key)');
        // Numeric keys made as strings: canonical array indices.
        var idx = '' + (i + 1000000000) % 1000000000;
        if (arr[idx] !== i) fail('arr["' + idx + '"] = ' + arr[idx]);
        var notIdx = 'idx' + ((round * 64 + i) % 1500) + 'padding';
        if (arr[notIdx] !== undefined) fail('arr[' + notIdx + '] = ' + arr[notIdx]);
    }
    if (arr.length !== 64) fail('arr.length = ' + arr.length);
}
if (Object.keys(o).length !== 64) fail('Object.keys(o).length = ' + Object.keys(o).length);
if (failures) { console.log('FAIL stale_key_caches: ' + failures + ' of ' + checked); process.exit(1); }
console.log('ok stale_key_caches');
