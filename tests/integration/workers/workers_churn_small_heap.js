// Workers start and stop while the main thread allocates arrays, typed arrays,
// maps and sets under a small heap ceiling (the test is registered with
// PROTOCORE_HEAP_LIMIT_CELLS). Each worker space installs its own built-ins
// and is destroyed when the worker ends; nothing the main thread makes may
// refer to a dead worker's prototypes, and no main-thread value may be
// collected or overwritten while workers come and go
// (docs/MEMORY_SAFETY_PROPOSALS.md, section 1). Meant to run under
// AddressSanitizer as well, where a read of a destroyed space's cell aborts.
//
// Asserting test: exits 1 on the first wrong prototype or value.
'use strict';
const { Worker } = require('worker_threads');

const ROUNDS = Number(process.argv[2] || 6);
const PER_ROUND = 2;
const child = __dirname + '/prototypes_after_worker_child.js';
let failures = 0;
function check(what, cond) {
    if (!cond && failures++ < 20) console.log('FAIL ' + what);
}

// Main-thread data that must survive every round intact.
const keep = { arrays: [], typed: [], maps: [], sets: [], made: [] };
function allocate(round) {
    for (let i = 0; i < 400; i++) {
        const a = [round, i].map(x => x + 1);
        const t = Float64Array.of(round, i);
        const m = new Map([[i, round]]);
        const s = new Set([round * 1000 + i]);
        if (i % 40 === 0) { keep.arrays.push(a); keep.typed.push(t); keep.maps.push(m); keep.sets.push(s); keep.made.push([round, i]); }
    }
}
function verify(round) {
    for (let r = 0; r < keep.arrays.length; r++) {
        const a = keep.arrays[r], t = keep.typed[r], m = keep.maps[r], s = keep.sets[r];
        check('array prototype r' + round, Object.getPrototypeOf(a) === Array.prototype);
        check('typed prototype r' + round, Object.getPrototypeOf(t) === Float64Array.prototype);
        check('map prototype r' + round, Object.getPrototypeOf(m) === Map.prototype);
        check('set prototype r' + round, Object.getPrototypeOf(s) === Set.prototype);
        const rr = keep.made[r][0], i = keep.made[r][1];
        check('array value r' + round, a[0] === rr + 1 && a[1] === i + 1 && a.join() === (rr + 1) + ',' + (i + 1));
        check('typed value r' + round, t[0] === rr && t[1] === i);
        check('map value r' + round, m.get(i) === rr);
        check('set value r' + round, s.has(rr * 1000 + i));
    }
    const u = Uint8Array.of(300);
    check('fresh typed array r' + round, Object.getPrototypeOf(u) === Uint8Array.prototype && u[0] === 44);
    check('fresh map r' + round, Object.getPrototypeOf(new Map()) === Map.prototype);
}

function round(r) {
    if (r === ROUNDS) {
        verify('end');
        // Under the ceiling the test is registered with, the main thread
        // must have collected while workers came and went.
        if (typeof protoCore !== 'undefined' && process.env.PROTOCORE_HEAP_LIMIT_CELLS)
            check('collections ran on the main thread', protoCore.gcStats().cycles > 0);
        if (failures) { console.log('workers/workers_churn_small_heap: ' + failures + ' failures'); process.exit(1); }
        const cycles = (typeof protoCore !== 'undefined') ? protoCore.gcStats().cycles : 'n/a';
        console.log('workers/workers_churn_small_heap: ' + ROUNDS + ' rounds, ' +
                    keep.arrays.length + ' kept values, ' + cycles +
                    ' collections on the main thread, all checks passed');
        return;
    }
    let pending = PER_ROUND;
    for (let k = 0; k < PER_ROUND; k++) {
        const w = new Worker(child);
        w.on('error', e => check('worker error: ' + e, false));
        w.on('exit', () => {
            allocate(r);
            verify(r);
            if (--pending === 0) round(r + 1);
        });
    }
    allocate(r);  // while the workers run
}
round(0);
