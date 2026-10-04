// Values written to a typed array go through ToNumber, or ToBigInt for
// BigInt64Array / BigUint64Array (ECMA-262 §10.4.5.16 TypedArraySetElement),
// with the side effects of valueOf / toString / Symbol.toPrimitive in order,
// and before the index is checked. protoJS stored 0 for any value that was not
// already a Number: an object with valueOf, a string, a boolean.
// Expected values were checked against Node.js.

let failures = 0;
function check(actual, expected, label) {
    const a = JSON.stringify(actual), e = JSON.stringify(expected);
    if (a !== e) { failures++; console.log("FAIL " + label + ": expected " + e + ", got " + a); }
}
function thrownName(fn) { try { fn(); return "no throw"; } catch (e) { return e && e.name ? e.name : String(e); } }

const log = [];
const num = (v, tag) => ({ valueOf() { log.push(tag); return v; } });

// Element writes.
const f64 = new Float64Array(3);
f64[0] = num(1.5, "a");
f64[1] = { toString() { log.push("b"); return "2.5"; }, valueOf: undefined };
f64[2] = { [Symbol.toPrimitive](hint) { log.push("c:" + hint); return 3.25; } };
check(Array.from(f64), [1.5, 2.5, 3.25], "Float64Array element writes");
check(log.splice(0), ["a", "b", "c:number"], "coercion order");

const i8 = new Int8Array(4);
i8[0] = num(300, "x"); i8[1] = "-129"; i8[2] = true; i8[3] = null;
check(Array.from(i8), [44, 127, 1, 0], "Int8Array wraps after ToNumber");
const u8c = new Uint8ClampedArray(3);
u8c[0] = num(300, "y"); u8c[1] = "2.5"; u8c[2] = num(-4, "z");
check(Array.from(u8c), [255, 2, 0], "Uint8ClampedArray clamps after ToNumber");
const u32 = new Uint32Array(2);
u32[0] = 2 ** 53 + 2; u32[1] = -1e20;
check(Array.from(u32), [2, 2632974336], "Uint32Array modulo 2^32 of large doubles");
log.length = 0;

// Out-of-range index: still coerced (side effect), nothing written.
const small = new Int16Array(1);
small[5] = num(7, "oob");
check([log.splice(0), small.length, small[5]], [["oob"], 1, undefined], "out-of-range write coerces");

// Exceptions propagate; a BigInt into a Number array is a TypeError.
check(thrownName(() => { f64[0] = { valueOf() { throw new RangeError("v"); } }; }), "RangeError", "valueOf throws");
check(thrownName(() => { f64[0] = 1n; }), "TypeError", "BigInt into Float64Array");
check(thrownName(() => { f64[0] = { valueOf() { return 1n; } }; }), "TypeError", "valueOf returning BigInt");
check(thrownName(() => { f64[0] = Symbol("s"); }), "TypeError", "Symbol into Float64Array");

// BigInt arrays: ToBigInt.
const b64 = new BigInt64Array(2);
b64[0] = { valueOf() { log.push("big"); return 7n; } };
b64[1] = "12";
check([String(b64[0]), String(b64[1]), log.splice(0)], ["7", "12", ["big"]], "BigInt64Array ToBigInt");
check(thrownName(() => { b64[0] = 1; }), "TypeError", "Number into BigInt64Array");

// Built-ins that write elements.
const u8 = new Uint8Array(3);
u8.set([num(9, "s0"), "4", true]);
check([Array.from(u8), log.splice(0)], [[9, 4, 1], ["s0"]], "set() from an array");
check(Array.from(Float32Array.from([num(0.5, "f")])), [0.5], "from()");
check(Array.from(Int16Array.of(num(3, "o"), "5")), [3, 5], "of()");
check(Array.from(new Int16Array([num(12, "k"), "13"])), [12, 13], "constructor from an array");
log.length = 0;
const filled = new Uint8Array(3);
filled.fill(num(5, "fill"));
check([Array.from(filled), log.splice(0)], [[5, 5, 5], ["fill"]], "fill() coerces once");
const viaDefine = new Float64Array(1);
Object.defineProperty(viaDefine, "0", { value: num(6.5, "def") });
check(viaDefine[0], 6.5, "defineProperty value");

if (failures) { console.log(failures + " failure(s)"); process.exit(1); }
console.log("typed_array_coercion: OK");
