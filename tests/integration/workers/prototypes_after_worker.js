// Every built-in prototype is per space. A worker_threads worker has its own
// space and runs the built-in installers for its own global; before the fix
// several installers published the prototype they made in a process-wide C++
// static, so once a worker had run, arrays made by map() and typed arrays made
// by Uint8Array.of() on the MAIN thread got the worker's prototypes -- objects
// of a space that had since been destroyed (docs/MEMORY_SAFETY_PROPOSALS.md,
// section 1).
//
// Asserting test: checks the prototype (and the element behaviour) of every
// kind of object the main thread makes, before a worker starts, while it runs,
// and after it has exited; exits 1 on any failure.
'use strict';
const { Worker } = require('worker_threads');

let failures = 0;
function check(phase, what, cond) {
    if (!cond) { failures++; console.log('FAIL [' + phase + '] ' + what); }
}

const TA = [Int8Array, Uint8Array, Uint8ClampedArray, Int16Array, Uint16Array, Int32Array,
            Uint32Array, Float32Array, Float64Array, BigInt64Array, BigUint64Array];
const ArrayIteratorProto = Object.getPrototypeOf([][Symbol.iterator]());
const MapIteratorProto = Object.getPrototypeOf(new Map()[Symbol.iterator]());
const SetIteratorProto = Object.getPrototypeOf(new Set()[Symbol.iterator]());

function probe(phase) {
    const AP = Array.prototype;
    const arrays = {
        'literal': [1, 2], 'map': [1, 2].map(x => x), 'filter': [1, 2].filter(() => true),
        'slice': [1, 2].slice(0), 'concat': [1].concat([2]), 'Array.from': Array.from([1, 2]),
        'Array.of': Array.of(1, 2), 'new Array': new Array(2), 'Array(n)': Array(2),
        'splice': [1, 2, 3].splice(0, 1), 'flat': [[1], [2]].flat(), 'JSON.parse': JSON.parse('[1,2]'),
        'split': 'a,b'.split(','), 'Object.keys': Object.keys({ a: 1 }), 'spread': [...[1, 2]],
        'rest': ((...r) => r)(1, 2), 'toSorted': [2, 1].toSorted(),
    };
    for (const k in arrays) {
        const a = arrays[k];
        check(phase, 'Array ' + k + ' prototype', Object.getPrototypeOf(a) === AP);
        check(phase, 'Array ' + k + ' instanceof', a instanceof Array);
        check(phase, 'Array ' + k + ' has join', typeof a.join === 'function');
    }
    for (const C of TA) {
        const big = C === BigInt64Array || C === BigUint64Array;
        const one = big ? 1n : 1;
        const made = {
            'new': new C(2), 'of': C.of(one), 'from': C.from([one]),
            'map': C.of(one).map(x => x), 'slice': C.of(one, one).slice(1),
            'subarray': C.of(one, one).subarray(1),
            'buffer': new C(new ArrayBuffer(8 * C.BYTES_PER_ELEMENT)),
        };
        for (const k in made) {
            const t = made[k];
            check(phase, C.name + ' ' + k + ' prototype', Object.getPrototypeOf(t) === C.prototype);
            t[0] = one;
            check(phase, C.name + ' ' + k + ' element write/read', t[0] === one);
        }
        // Element coercion goes through the typed-array behaviour.
        if (C === Uint8Array) {
            const u = new Uint8Array(1); u[0] = 257;
            check(phase, 'Uint8Array wraps 257 to 1', u[0] === 1);
        }
    }
    const bufs = { 'new': new ArrayBuffer(4), 'slice': new ArrayBuffer(4).slice(1),
                   'typed .buffer': new Uint8Array(4).buffer };
    for (const k in bufs)
        check(phase, 'ArrayBuffer ' + k + ' prototype',
              Object.getPrototypeOf(bufs[k]) === ArrayBuffer.prototype);
    check(phase, 'Map prototype', Object.getPrototypeOf(new Map()) === Map.prototype);
    check(phase, 'Set prototype', Object.getPrototypeOf(new Set()) === Set.prototype);
    const m = new Map([[1, 'a']]);
    check(phase, 'Map get', m.get(1) === 'a');
    check(phase, 'Set has', new Set([7]).has(7));
    check(phase, 'array iterator prototype',
          Object.getPrototypeOf([1].values()) === ArrayIteratorProto);
    check(phase, 'map iterator prototype',
          Object.getPrototypeOf(new Map().entries()) === MapIteratorProto);
    check(phase, 'set iterator prototype',
          Object.getPrototypeOf(new Set().values()) === SetIteratorProto);
}

probe('before');
const w = new Worker(__dirname + '/prototypes_after_worker_child.js');
let reported = null;
w.on('message', m => { reported = m; probe('while-running'); });
w.on('error', e => { failures++; console.log('FAIL worker error: ' + e); });
w.on('exit', () => {
    check('after-exit', 'worker reported its objects', reported === 24);
    probe('after-exit');
    // Allocate enough for a collection to run over whatever the dead space left
    // behind, then look again.
    let keep = [];
    for (let i = 0; i < 20000; i++) keep.push([i].map(x => x + 1));
    check('after-gc', 'arrays survive', keep[19999][0] === 20000);
    probe('after-gc');
    if (failures) {
        console.log('workers/prototypes_after_worker: ' + failures + ' failures');
        process.exit(1);
    }
    console.log('workers/prototypes_after_worker: all checks passed');
});
