// SameValueZero through Map and Set (src/HashedCollection.cpp): which keys
// are one entry and which are distinct, for every kind of value, including
// the representations protoJS can hold one number in (a SmallInteger or a
// double) and strings built in different ways. Passes under Node.js.

let failures = 0;
let checks = 0;
function check(name, cond) {
    checks++;
    if (!cond) { failures++; console.log("FAILED: " + name); }
}

const m = new Map();
// Numbers: by value, whatever produced them; NaN is one key; -0 is +0.
m.set(1, "one");
check("1 and 1.0", m.get(1.0) === "one" && m.get(Math.floor(1.5)) === "one" && m.get(3 / 3) === "one");
check("number vs string", m.get("1") === undefined);
m.set(NaN, "nan");
check("NaN finds NaN", m.get(0 / 0) === "nan" && m.get(Number("x")) === "nan" && m.has(NaN));
m.set(-0, "zero");
check("-0 stored as +0", m.get(0) === "zero" && Object.is([...m.keys()].find((k) => k === 0), 0));
check("0.5 vs 0.25 * 2", (m.set(0.5, "half"), m.get(0.25 * 2)) === "half");
const big = 2 ** 53;
m.set(big, "big");
check("2**53 by value", m.get(9007199254740992) === "big" && m.get(big + 1) === "big");
check("2**53 - 1 distinct", m.get(big - 1) === undefined);
// Strings by content, however built.
m.set("ab", "AB");
const built = ["a", "b"].join("");
check("joined string", m.get(built) === "AB" && m.get("a" + "b") === "AB" && m.get("abc".slice(0, 2)) === "AB");
check("case matters", m.get("AB") === undefined);
const long1 = "x".repeat(200) + "y";
m.set(long1, "long");
check("long string built twice", m.get("x".repeat(100) + "x".repeat(100) + "y") === "long");
// BigInt by value.
m.set(10n, "ten");
check("BigInt by value", m.get(10n) === "ten" && m.get(BigInt(10)) === "ten" && m.get(5n * 2n) === "ten");
check("BigInt vs number", m.get(10) === undefined);
const huge = new Map([[2n ** 70n, "big"], [-(2n ** 65n), "neg"]]);
check("large BigInt keys", huge.get(2n ** 70n) === "big" && huge.get(-(2n ** 65n)) === "neg" &&
      new Set([2n ** 80n, 2n ** 80n]).size === 1);
// Identity for objects, arrays, functions, symbols.
const o = { a: 1 };
const sym = Symbol("s");
m.set(o, "obj").set(sym, "sym").set(Symbol.for("reg"), "reg");
check("object identity", m.get(o) === "obj" && m.get({ a: 1 }) === undefined);
check("symbol identity", m.get(sym) === "sym" && m.get(Symbol("s")) === undefined);
check("registered symbol", m.get(Symbol.for("reg")) === "reg");
// null, undefined, booleans.
m.set(null, "null").set(undefined, "undef").set(true, "t").set(false, "f");
check("null/undefined/booleans", m.get(null) === "null" && m.get(undefined) === "undef" &&
      m.get(true) === "t" && m.get(false) === "f" && m.get(0) === "zero");
check("size", m.size === 15);
// Overwrite keeps the first key object and its position.
m.set(1.0, "uno");
check("overwrite keeps position", [...m.keys()][0] === 1 && m.get(1) === "uno");
// Delete through another representation.
check("delete by equal key", m.delete(1.0) && !m.has(1) && m.delete(NaN) && !m.has(NaN) && m.delete(-0) && !m.has(0));
check("delete BigInt by value", m.delete(BigInt(10)) && !m.has(10n));
check("size after deletes", m.size === 11);

// Set: the same rules.
const s = new Set([1, 1.0, "1", NaN, 0 / 0, 0, -0, 1n, BigInt(1), "ab", ["a", "b"].join("")]);
check("set dedupes", s.size === 6);
check("set has", s.has(1) && s.has("1") && s.has(NaN) && s.has(-0) && s.has(1n) && s.has("a" + "b"));
s.delete(-0);
check("set delete -0", !s.has(0) && s.size === 5);
check("set order", [...s].map(String).join() === "1,1,NaN,1,ab");
if (typeof s.union === "function") {
    const u = new Set([1, 2]).union(new Set([2.0, 3]));
    const i = new Set(["a", "b"]).intersection(new Set(["b", "c"]));
    check("union / intersection", [...u].join() === "1,2,3" && [...i].join() === "b");
}

// Many colliding-class keys: integers that agree in their low 31 bits.
const c = new Map();
for (let k = 0; k < 64; k++) c.set(1 + k * 2 ** 31, k);
let ok = c.size === 64;
for (let k = 0; k < 64; k++) ok = ok && c.get(1 + k * 2 ** 31) === k;
check("integers with equal low bits", ok);

if (failures === 0) {
    console.log("map_set_semantics: all " + checks + " checks passed");
} else {
    console.log("map_set_semantics: " + failures + " of " + checks + " checks FAILED");
    if (typeof process !== "undefined") process.exit(1);
}
