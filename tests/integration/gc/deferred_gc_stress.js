// Deferreds allocating under a small heap ceiling, in parallel with the main
// thread (run by tests/cli/gc-stress.sh with PROTOCORE_HEAP_LIMIT_CELLS).
//
// Every pool thread is a protoCore thread of the main thread's space: a
// collection can start only when all of them have parked, the objects each one
// is building are reachable only from its own frames, and the values handed
// back to the main thread are pinned until the Deferred settles. This script
// makes all of that happen many times per run:
//
//   - a shared table, built once on the main thread, read by every Deferred
//     while it allocates (a reader must never see a freed cell);
//   - each Deferred builds short-lived garbage and one long-lived result
//     (an array of objects and strings), which it returns;
//   - the main thread allocates garbage of its own while they run;
//   - every result is checked in full on the main thread.
//
// Prints "ok deferred_gc_stress" when every check passed.

const failures = [];
function check(name, ok, detail) {
    if (!ok) failures.push(name + (detail !== undefined ? ' -- ' + detail : ''));
}

const table = [];
for (let i = 0; i < 3000; i++) table.push({ id: i, name: 'item-' + i, weight: i % 17 });
let tableWeight = 0;
for (let i = 0; i < table.length; i++) tableWeight += table[i].weight;

function work(seed) {
    // Garbage: strings and objects that die at once.
    let junk = 0;
    for (let i = 0; i < 2000; i++) {
        const o = { a: i, s: 'x' + (seed * 7 + i) };
        junk += o.s.length;
    }
    // A long-lived result: survives every collection until it is checked.
    const out = [];
    for (let i = 0; i < 300; i++) out.push({ k: seed * 1000 + i, label: 'r' + seed + '-' + i });
    // Read the shared table while other threads allocate.
    let w = 0;
    for (let i = 0; i < table.length; i++) w += table[i].weight;
    return { seed, out, w, junk };
}

async function main() {
    const ROUNDS = 6, WIDTH = 8;
    for (let r = 0; r < ROUNDS; r++) {
        const ds = [];
        for (let j = 0; j < WIDTH; j++) {
            const seed = r * WIDTH + j;
            ds.push(new Deferred(() => work(seed)));
        }
        // The main thread churns memory while the pool runs.
        let mainJunk = [];
        for (let i = 0; i < 3000; i++) {
            mainJunk.push({ i, t: 'm' + i });
            if (mainJunk.length > 200) mainJunk = [];
        }
        const results = await Promise.all(ds);
        for (let j = 0; j < WIDTH; j++) {
            const seed = r * WIDTH + j;
            const res = results[j];
            check('round ' + r + ' result ' + j + ' seed', res && res.seed === seed);
            check('round ' + r + ' result ' + j + ' table weight', res && res.w === tableWeight,
                  res && res.w);
            let ok = res && res.out.length === 300, bad = '';
            if (!ok) bad = 'length ' + (res && res.out.length);
            for (let i = 0; ok && i < 300; i++) {
                const e = res.out[i];
                ok = e.k === seed * 1000 + i && e.label === 'r' + seed + '-' + i;
                if (!ok) bad = 'element ' + i + ': k=' + (e && e.k) + ' label=' + (e && e.label);
            }
            check('round ' + r + ' result ' + j + ' contents', ok, bad);
        }
    }
    // The shared table survived intact.
    let w = 0, names = true;
    for (let i = 0; i < table.length; i++) {
        w += table[i].weight;
        if (table[i].name !== 'item-' + i) names = false;
    }
    check('shared table weight', w === tableWeight);
    check('shared table names', names);
}

main().then(() => {
    if (failures.length) {
        console.log('deferred_gc_stress: ' + failures.length + ' check(s) failed');
        for (const f of failures.slice(0, 10)) console.log('  FAIL: ' + f);
        process.exit(1);
    }
    console.log('ok deferred_gc_stress');
}, (e) => {
    console.log('deferred_gc_stress: main threw ' + e);
    process.exit(1);
});
