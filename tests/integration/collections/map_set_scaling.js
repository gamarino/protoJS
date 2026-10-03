// Map and Set: SameValueZero semantics, insertion order across deletes, hash
// collisions, and linear cost. Map.set and Set.add used to walk every entry
// on each insertion (4,000 Map.set calls allocated about 1 GB); the last
// section inserts enough keys that a quadratic implementation would not
// finish in the test's time limit. The file passes unchanged under Node.js.

let failures = 0;
let checks = 0;
function check(name, cond) {
    checks++;
    if (!cond) { failures++; console.log("FAILED: " + name); }
}

// SameValueZero keys.
const m = new Map();
m.set(NaN, "nan").set(-0, "zero").set("1", "str1").set(1, "num1");
const objKey = { k: 1 };
m.set(objKey, "obj");
check("NaN key", m.get(NaN) === "nan" && m.has(0 / 0));
check("-0 and +0 are one key", m.get(0) === "zero" && m.get(-0) === "zero");
check("string and number keys differ", m.get("1") === "str1" && m.get(1) === "num1");
check("object key by identity", m.get(objKey) === "obj" && m.get({ k: 1 }) === undefined);
check("integral double key", m.get(Math.floor(1.5)) === "num1" && m.get(2 / 2) === "num1");
check("size", m.size === 5);

// Keys whose hashes collide (the integer hash keeps the low 31 bits).
const c = new Map();
const a = 1, b = 2 ** 31 + 1, d = 2 ** 32 + 1;
c.set(a, "a").set(b, "b").set(d, "d");
check("colliding keys stay distinct", c.get(a) === "a" && c.get(b) === "b" && c.get(d) === "d");
c.delete(b);
check("delete one colliding key", !c.has(b) && c.get(a) === "a" && c.get(d) === "d" && c.size === 2);
c.set(b, "b2");
check("re-add colliding key", c.get(b) === "b2" && c.size === 3);

// Insertion order: a deleted and re-added key moves to the end.
const o = new Map([["c", 1], ["a", 2], ["b", 3]]);
o.delete("a");
o.set("a", 4);
o.set("c", 5);  // update keeps its position
check("order after delete/re-add", [...o.keys()].join() === "c,b,a" && o.get("c") === 5);
o.clear();
o.set("z", 1);
check("clear then set", o.size === 1 && [...o.entries()].join() === "z,1");

// Set: same rules.
const s = new Set(["x", "y", "z"]);
s.delete("x");
s.add("x");
s.add("y");
check("set order after delete/re-add", [...s].join() === "y,z,x" && s.size === 3);
const sn = new Set([NaN, 0, -0, 1, Math.ceil(0.5)]);
check("set SameValueZero", sn.size === 3 && sn.has(NaN) && sn.has(-0));

// Map.groupBy groups in first-seen order.
if (typeof Map.groupBy === "function") {
    const g = Map.groupBy([1, 2, 3, 4, 5, 6], (v) => (v % 3 === 0 ? "three" : v % 2 ? "odd" : "even"));
    check("groupBy", [...g.keys()].join() === "odd,even,three" && g.get("odd").join() === "1,5"
          && g.get("even").join() === "2,4" && g.get("three").join() === "3,6");
}

// Linear cost: 60,000 string keys and 60,000 integer keys.
const N = 60000;
const big = new Map();
for (let i = 0; i < N; i++) big.set("key" + i, i);
for (let i = 0; i < N; i++) big.set(i, -i);
let sum = 0;
for (let i = 0; i < N; i += 7) sum += big.get("key" + i) + big.get(i);
check("big map size", big.size === 2 * N);
check("big map lookups", sum === 0);
for (let i = 0; i < N; i += 2) big.delete("key" + i);
check("big map deletes", big.size === N + N / 2 && !big.has("key0") && big.get("key1") === 1);
const bigSet = new Set();
for (let i = 0; i < N; i++) bigSet.add("v" + (i % 1000));
check("big set dedupe", bigSet.size === 1000);

if (failures === 0) {
    console.log("map_set_scaling: all " + checks + " checks passed");
} else {
    console.log("map_set_scaling: " + failures + " of " + checks + " checks FAILED");
    if (typeof process !== "undefined") process.exit(1);
}
