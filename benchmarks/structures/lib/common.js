// Helpers shared by every structure workload, in protoJS and in Node.js.
//
// Everything here is deterministic and uses only integer arithmetic that is
// exact in IEEE doubles, so both runtimes compute bit-identical results.

// Park-Miller minimal standard generator: 48271 * (2^31 - 2) < 2^53, so the
// product is exact in a double.
function makeRng(seed) {
    let s = (seed % 2147483646) + 1;
    return {
        next() { s = (s * 48271) % 2147483647; return s; },
        // Integer in [0, n).
        int(n) { s = (s * 48271) % 2147483647; return s % n; },
    };
}

// FNV-1a, 32 bits, over a string's UTF-16 code units.
function hashString(h, str) {
    for (let i = 0; i < str.length; i++) {
        h = Math.imul(h ^ str.charCodeAt(i), 16777619) >>> 0;
    }
    return h;
}

// Fold a non-negative integer (up to 2^53) into a 32-bit hash.
function hashInt(h, n) {
    const lo = n % 4294967296;
    const hi = (n - lo) / 4294967296;
    h = Math.imul(h ^ lo, 16777619) >>> 0;
    h = Math.imul(h ^ hi, 16777619) >>> 0;
    return h;
}

const FNV_OFFSET = 2166136261;

// Hash of a result made of non-negative integers, strings and arrays of them
// (results never rely on object key order, which differs between runtimes).
function hashValue(h, v) {
    if (typeof v === "number") {
        if (!(v >= 0 && v === Math.floor(v) && v <= 9007199254740991))
            throw new Error("hashValue: not a non-negative safe integer: " + v);
        return hashInt(Math.imul(h ^ 1, 16777619) >>> 0, v);
    }
    if (typeof v === "string") return hashString(Math.imul(h ^ 2, 16777619) >>> 0, v);
    if (Array.isArray(v)) {
        h = hashInt(Math.imul(h ^ 3, 16777619) >>> 0, v.length);
        for (let i = 0; i < v.length; i++) h = hashValue(h, v[i]);
        return h;
    }
    throw new Error("hashValue: unsupported value " + typeof v);
}

const LETTERS = "abcdefghijklmnopqrstuvwxyz";

// A pronounceable lowercase word of `len` letters.
function makeWord(rng, len) {
    let w = "";
    for (let i = 0; i < len; i++) w += LETTERS[rng.int(26)];
    return w;
}

module.exports = { makeRng, hashString, hashInt, hashValue, FNV_OFFSET, makeWord };
