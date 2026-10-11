// Child of prototypes_after_worker.js: makes one of every kind of object whose
// prototype the main thread checks, so that every installer and every
// prototype cache of this worker's space has run, then reports and ends.
const made = [
    [1, 2].map(x => x), Array.from([1]), Array.of(1), new Array(3), [1].concat([2]),
    new ArrayBuffer(8), new ArrayBuffer(8).slice(0, 4), new DataView(new ArrayBuffer(4)),
    new Map([[1, 2]]), new Set([1]), new Map().keys(), new Set().values(), [].values(),
    Int8Array.of(1), Uint8Array.of(1), Uint8ClampedArray.of(1), Int16Array.of(1),
    Uint16Array.of(1), Int32Array.of(1), Uint32Array.of(1), Float32Array.of(1),
    Float64Array.of(1), BigInt64Array.of(1n), BigUint64Array.of(1n),
];
(typeof parentPort !== "undefined" ? parentPort : require("worker_threads").parentPort)
    .postMessage(made.length);
