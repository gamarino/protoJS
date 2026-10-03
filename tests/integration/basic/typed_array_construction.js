// Typed arrays built from every source the constructors accept (ECMA-262
// §23.2.5.1: a length, an ArrayBuffer, another typed array, an iterable or an
// array-like object), read back through indexing, and copied out again with
// Array.from, spread and %TypedArray%.from / of. `new Uint8Array([5])` used to
// produce an empty array, and indexing a typed array read undefined even when
// its buffer held the values. Passes under Node.js.
let failures = 0, checks = 0;
function check(name, cond) { checks++; if (!cond) { failures++; console.log("FAILED: " + name); } }
function same(ta, expected) {
    if (ta.length !== expected.length) return false;
    for (let i = 0; i < expected.length; i++) if (ta[i] !== expected[i]) return false;
    return true;
}

// Indexing a typed array reads and writes its buffer.
const z = new Uint8Array(3);
check("length ctor", z.length === 3 && z[0] === 0 && z[2] === 0);
z[1] = 7;
check("index write/read", z[1] === 7 && z.join() === "0,7,0");
check("out of bounds read", z[3] === undefined);
check("in operator", (1 in z) && !(3 in z) && ("2" in z));
z["2"] = 5;
check("string index", z["2"] === 5 && z[2] === 5);
z[2] = 0;
z.fill(4);
check("fill visible through index", z[0] === 4 && z[2] === 4);

// From arrays, for every element kind.
check("Uint8Array([5])", same(new Uint8Array([5]), [5]));
check("Uint8Array([1,2,3])", same(new Uint8Array([1, 2, 3]), [1, 2, 3]));
check("Int8Array wraps", same(new Int8Array([127, 128, -129]), [127, -128, 127]));
check("Uint8Array wraps", same(new Uint8Array([256, -1]), [0, 255]));
check("Uint8ClampedArray clamps", same(new Uint8ClampedArray([300, -5, 1.5]), [255, 0, 2]));
check("Int16Array", same(new Int16Array([-1, 32768]), [-1, -32768]));
check("Uint16Array", same(new Uint16Array([65535, 65536]), [65535, 0]));
check("Int32Array", same(new Int32Array([-5, 2147483648]), [-5, -2147483648]));
check("Uint32Array", same(new Uint32Array([4294967295, -1]), [4294967295, 4294967295]));
check("Float32Array", same(new Float32Array([1.5, -2.25]), [1.5, -2.25]));
check("Float64Array", same(new Float64Array([1.5, 0.1]), [1.5, 0.1]));
check("BigInt64Array length", new BigInt64Array(2).length === 2);
check("empty array", new Uint8Array([]).length === 0);
check("no argument", new Uint8Array().length === 0);

// From array-likes, iterables and other typed arrays.
check("array-like", same(new Uint8Array({ length: 3, 0: 7, 1: 8 }), [7, 8, 0]));
check("Set", same(new Uint16Array(new Set([1, 2, 3])), [1, 2, 3]));
check("generator", same(new Int32Array((function* () { yield 4; yield 5; })()), [4, 5]));
check("typed array copy", same(new Uint8Array(new Uint8Array([9, 10])), [9, 10]));
check("typed array conversion", same(new Int8Array(new Float64Array([1.9, -200])), [1, 56]));
const src = new Uint8Array([1, 2]);
const copy = new Uint8Array(src);
copy[0] = 99;
check("typed array copy is independent", src[0] === 1);

// Views over an ArrayBuffer share its bytes.
const buf = new ArrayBuffer(8);
const whole = new Uint8Array(buf);
check("buffer view length", whole.length === 8);
const tail = new Uint8Array(buf, 4);
const mid = new Uint8Array(buf, 2, 2);
check("offset view lengths", tail.length === 4 && mid.length === 2);
whole[4] = 42; whole[3] = 17;
check("views share the buffer", tail[0] === 42 && mid[1] === 17);
const words = new Uint16Array(buf);
check("wider view length", words.length === 4);

// Copying out.
check("Array.from", Array.from(new Uint8Array([1, 2])).join() === "1,2");
check("Array.from map", Array.from(new Uint8Array([1, 2]), (x) => x * 10).join() === "10,20");
check("spread", [...new Uint8Array([3, 4])].join() === "3,4");
check("Array.prototype.join.call", Array.prototype.join.call(new Uint8Array([5, 6])) === "5,6");
check("for-of", (() => { let s = 0; for (const v of new Uint8Array([1, 2, 3])) s += v; return s; })() === 6);
check("Uint8Array.from array", same(Uint8Array.from([1, 2]), [1, 2]));
check("Uint8Array.from Set", same(Uint8Array.from(new Set([5, 6])), [5, 6]));
check("Uint8Array.from map", same(Uint8Array.from([1, 2], (x) => x + 1), [2, 3]));
check("Uint8Array.of", same(Uint8Array.of(1, 2), [1, 2]));
check("Float64Array.of", same(Float64Array.of(0.5), [0.5]));

if (failures === 0) console.log("typed_array_construction: all " + checks + " checks passed");
else { console.log("typed_array_construction: " + failures + " of " + checks + " checks FAILED"); process.exit(1); }
