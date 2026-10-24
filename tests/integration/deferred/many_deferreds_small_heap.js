// Many Deferreds under a small heap budget (PROTOCORE_HEAP_LIMIT_CELLS, set by
// tests/CMakeLists.txt): each builds a structure on a pool thread of the main
// space and returns it; the main thread keeps every eighth result and drops
// the rest. Collections must run while the pool allocates, every result must
// arrive intact, the kept results must survive them, and the process must
// hold one ProtoSpace throughout.
//
// Deferred is protoJS-specific, so this file does not run under Node.js.
// Asserting test: prints the failures and exits 1 when anything is wrong.

const TASKS = 2000;
const SIZE = 400;
const BATCH = 100;
const kept = [];
let failures = 0;
function fail(msg) { if (failures++ < 10) console.log('FAIL ' + msg); }

function task(t) {
    return new Deferred(function () {
        const a = [];
        for (let i = 0; i < SIZE; i++) a.push({ t: t, i: i, s: 't' + t + '-' + i });
        return a;
    });
}

function check(a, t) {
    if (!a || a.length !== SIZE) return false;
    return a[0].t === t && a[SIZE - 1].i === SIZE - 1 && a[SIZE - 1].s === 't' + t + '-' + (SIZE - 1);
}

const cyclesBefore = protoCore.gcStats().cycles;
function batch(start) {
    const ds = [];
    for (let t = start; t < start + BATCH; t++) ds.push(task(t));
    return Promise.all(ds).then(function (rs) {
        for (let k = 0; k < rs.length; k++) {
            const t = start + k;
            if (!check(rs[k], t)) fail('task ' + t + ' returned a damaged structure');
            if (t % 8 === 0) kept.push(rs[k]);
        }
        if (protoCore.gcStats().liveSpaces !== 1) fail('more than one ProtoSpace');
        if (start + BATCH < TASKS) return batch(start + BATCH);
    });
}

batch(0).then(function () {
    for (let k = 0; k < kept.length; k++) {
        if (!check(kept[k], k * 8)) fail('kept result ' + (k * 8) + ' damaged after collections');
    }
    const cycles = protoCore.gcStats().cycles - cyclesBefore;
    if (cycles < 1) fail('no collection ran: the test proved nothing');
    if (failures) process.exit(1);
    console.log('deferred/many_deferreds_small_heap: ' + TASKS + ' Deferreds, ' + kept.length +
                ' kept, collections ran, 1 space');
}, function (e) { console.log('FAIL rejected: ' + e); process.exit(1); });
