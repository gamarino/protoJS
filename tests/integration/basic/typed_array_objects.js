// Typed arrays and ArrayBuffers as objects (ECMA-262 §23.2, §25.1):
// - their own keys are the element indices (Object.keys, for-in,
//   Object.getOwnPropertyNames, hasOwnProperty, getOwnPropertyDescriptor);
//   buffer / byteLength / byteOffset / length are accessors of
//   %TypedArray%.prototype, and ArrayBuffer.prototype.byteLength is one too;
// - Object.prototype.toString names the kind ([object Uint8Array]);
// - `class X extends Uint8Array` and Reflect.construct(Uint8Array, args,
//   newTarget) honour their arguments and newTarget's prototype;
// - BigInt64Array / BigUint64Array elements are BigInts;
// - a length argument goes through ToIndex: '3' -> 3, true -> 1, and a
//   negative or out-of-range length is a RangeError.
// Passes under Node.js.

var failures = 0;
var checks = 0;
function check(name, cond, detail) {
    checks++;
    if (!cond) {
        failures++;
        console.log("FAILED: " + name + (detail !== undefined ? " -- " + detail : ""));
    }
}
function threw(fn, Ctor) {
    try { fn(); } catch (e) { return e instanceof Ctor; }
    return false;
}
function json(v) { return JSON.stringify(v); }

// --- Own keys are the indices -------------------------------------------------------
var u = new Uint8Array([1, 2, 3]);
check("Object.keys", json(Object.keys(u)) === '["0","1","2"]', json(Object.keys(u)));
check("Object.getOwnPropertyNames", json(Object.getOwnPropertyNames(u)) === '["0","1","2"]',
      json(Object.getOwnPropertyNames(u)));
check("Object.values", json(Object.values(u)) === "[1,2,3]", json(Object.values(u)));
check("Object.entries", json(Object.entries(u)) === '[["0",1],["1",2],["2",3]]');
var forIn = [];
for (var k in u) forIn.push(k);
check("for-in", json(forIn) === '["0","1","2"]', json(forIn));
check("Reflect.ownKeys", json(Reflect.ownKeys(u)) === '["0","1","2"]', json(Reflect.ownKeys(u)));
check("hasOwnProperty index", u.hasOwnProperty(0) && u.hasOwnProperty("2") && !u.hasOwnProperty(3));
check("hasOwnProperty length", !u.hasOwnProperty("length") && !u.hasOwnProperty("buffer")
      && !u.hasOwnProperty("byteLength") && !u.hasOwnProperty("byteOffset"));
var d0 = Object.getOwnPropertyDescriptor(u, "1");
check("index descriptor", d0 && d0.value === 2 && d0.writable && d0.enumerable && d0.configurable,
      json(d0));
check("no length descriptor", Object.getOwnPropertyDescriptor(u, "length") === undefined);
u.extra = "x";
check("user property listed after the indices", json(Object.keys(u)) === '["0","1","2","extra"]',
      json(Object.keys(u)));

// --- Accessors ------------------------------------------------------------------
var TypedArrayPrototype = Object.getPrototypeOf(Uint8Array.prototype);
["buffer", "byteLength", "byteOffset", "length"].forEach(function (name) {
    var d = Object.getOwnPropertyDescriptor(TypedArrayPrototype, name);
    check("%TypedArray%.prototype." + name + " is an accessor",
          d && typeof d.get === "function" && d.set === undefined && !d.enumerable && d.configurable);
});
var buf = new ArrayBuffer(16);
var view = new Int16Array(buf, 4, 3);
check("length", view.length === 3 && typeof view.length === "number");
check("byteLength", view.byteLength === 6);
check("byteOffset", view.byteOffset === 4);
check("buffer", view.buffer === buf);
var getLength = Object.getOwnPropertyDescriptor(TypedArrayPrototype, "length").get;
check("length getter called directly", getLength.call(view) === 3);
check("length getter on a non-typed-array throws", threw(function () { getLength.call({}); }, TypeError));
check("ArrayBuffer byteLength", buf.byteLength === 16 && typeof buf.byteLength === "number");
var abd = Object.getOwnPropertyDescriptor(ArrayBuffer.prototype, "byteLength");
check("ArrayBuffer.prototype.byteLength is an accessor", abd && typeof abd.get === "function" && !abd.enumerable);
check("ArrayBuffer has no own byteLength", !buf.hasOwnProperty("byteLength"));
check("prototype methods are not enumerable", (function () {
    for (var key in TypedArrayPrototype) return false;
    return true;
})());

// --- Object.prototype.toString ------------------------------------------------------
check("toString Uint8Array", Object.prototype.toString.call(u) === "[object Uint8Array]",
      Object.prototype.toString.call(u));
check("toString Float64Array", Object.prototype.toString.call(new Float64Array(1)) === "[object Float64Array]");
check("toString BigInt64Array", Object.prototype.toString.call(new BigInt64Array(1)) === "[object BigInt64Array]");
check("toString ArrayBuffer", Object.prototype.toString.call(buf) === "[object ArrayBuffer]");
check("@@toStringTag of the prototype itself", TypedArrayPrototype[Symbol.toStringTag] === undefined);

// --- Subclasses and newTarget ---------------------------------------------------------
class Bytes extends Uint8Array {
    sum() { var s = 0; for (var i = 0; i < this.length; i++) s += this[i]; return s; }
}
var b = new Bytes(4);
check("subclass length", b.length === 4, String(b.length));
check("subclass prototype", Object.getPrototypeOf(b) === Bytes.prototype && b instanceof Uint8Array);
b[1] = 5; b[3] = 7;
check("subclass elements and methods", b.sum() === 12 && b[1] === 5);
check("subclass from an array", new Bytes([1, 2, 3]).sum() === 6);
function NewTarget() {}
NewTarget.prototype = Object.create(Uint8Array.prototype);
var r = Reflect.construct(Uint8Array, [3], NewTarget);
check("Reflect.construct length", r.length === 3, String(r.length));
check("Reflect.construct prototype", Object.getPrototypeOf(r) === NewTarget.prototype);
var r2 = Reflect.construct(Uint8Array, [[9, 8]]);
check("Reflect.construct without newTarget", r2.length === 2 && r2[0] === 9
      && Object.getPrototypeOf(r2) === Uint8Array.prototype);
var ab2 = Reflect.construct(ArrayBuffer, [8]);
check("Reflect.construct ArrayBuffer", ab2.byteLength === 8);

// --- BigInt64Array / BigUint64Array -----------------------------------------------------
var b64 = new BigInt64Array([1n, -2n]);
check("BigInt64Array element is a BigInt", typeof b64[0] === "bigint" && b64[0] === 1n && b64[1] === -2n);
b64[0] = 2n ** 63n;
check("BigInt64Array wraps", b64[0] === -(2n ** 63n));
var bu64 = new BigUint64Array(2);
bu64[0] = 5n;
bu64[1] = -1n;
check("BigUint64Array element", typeof bu64[0] === "bigint" && bu64[0] === 5n);
check("BigUint64Array large element", bu64[1] === 2n ** 64n - 1n, String(bu64[1]));
check("BigInt64Array rejects a Number", threw(function () { b64[0] = 1; }, TypeError));
check("Uint8Array rejects a BigInt", threw(function () { u[0] = 1n; }, TypeError));

// --- ToIndex for the length argument -------------------------------------------------
check("string length", new Uint8Array("3").length === 3);
check("true length", new Uint8Array(true).length === 1);
check("false length", new Uint8Array(false).length === 0);
check("null length", new Uint8Array(null).length === 0);
check("non-numeric string length", new Uint8Array("abc").length === 0);
check("fractional length", new Uint8Array(1.5).length === 1);
check("negative length", threw(function () { new Uint8Array(-1); }, RangeError));
check("negative string length", threw(function () { new Uint8Array("-2"); }, RangeError));
check("huge length", threw(function () { new Uint8Array(2 ** 53); }, RangeError));
check("ArrayBuffer string length", new ArrayBuffer("4").byteLength === 4);
check("ArrayBuffer negative length", threw(function () { new ArrayBuffer(-1); }, RangeError));
check("byteOffset ToIndex", new Uint8Array(new ArrayBuffer(8), "2").length === 6);

if (failures) {
    console.log("typed_array_objects: " + failures + " of " + checks + " checks failed");
    process.exit(1);
}
console.log("typed_array_objects: all " + checks + " checks passed");
