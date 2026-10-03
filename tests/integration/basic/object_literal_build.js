// Object literals are built immutable and made mutable after their last
// field (markObjectLiterals in src/runtime/BytecodeSpecialiser.cpp). Every
// shape of literal must behave exactly as before: the result is an ordinary
// extensible, writable object, and literal forms the pass leaves alone
// (spread, __proto__, methods, accessors, computed keys) mix correctly with
// the ones it rewrites. The file passes unchanged under Node.js.

let failures = 0;
let checks = 0;
function check(name, cond) {
    checks++;
    if (!cond) { failures++; console.log("FAILED: " + name); }
}
function sortedKeys(o) { return Object.keys(o).sort().join(","); }

// Plain fields; the object is mutable and extensible afterwards.
const a = { x: 1, y: "two", z: [3] };
check("plain values", a.x === 1 && a.y === "two" && a.z[0] === 3);
a.x = 10; a.w = 4; delete a.y;
check("writable after build", a.x === 10 && a.w === 4 && !("y" in a));
check("extensible, not frozen", Object.isExtensible(a) && !Object.isFrozen(a) && !Object.isSealed(a));
check("prototype is Object.prototype", Object.getPrototypeOf(a) === Object.prototype);
check("hasOwnProperty", a.hasOwnProperty("z") && !a.hasOwnProperty("toString"));
check("keys", sortedKeys(a) === "w,x,z");

// Identity: each evaluation creates a distinct object.
const made = [];
for (let i = 0; i < 3; i++) made.push({ i: i, sq: i * i });
check("distinct objects", made[0] !== made[1] && made[1] !== made[2]);
made[0].i = 99;
check("no shared state", made[1].i === 1 && made[2].sq === 4);

// Nested literals, arrays of literals, literal as argument and return value.
function id(v) { return v; }
const n = { a: { b: { c: 1 }, d: [{ e: 2 }, { f: 3 }] }, g: id({ h: 4 }) };
check("nested", n.a.b.c === 1 && n.a.d[0].e === 2 && n.a.d[1].f === 3 && n.g.h === 4);
n.a.b.c = 5; n.a.d[1].f = 6;
check("nested writable", n.a.b.c === 5 && n.a.d[1].f === 6);
function mk(i) { return { id: i, label: "o" + i }; }
check("returned literal", mk(7).id === 7 && mk(7).label === "o7");

// Values computed by calls, ternaries, logical operators and closures.
let calls = 0;
function f() { calls++; return calls; }
const c = { p: f(), q: calls > 0 ? "yes" : "no", r: null || "dflt", s: () => c.p };
check("computed values", c.p === 1 && c.q === "yes" && c.r === "dflt" && c.s() === 1);

// A value expression that throws: no half-built object escapes.
let thrown = false, partial;
try { partial = { ok: 1, bad: (() => { throw new Error("boom"); })(), after: 2 }; }
catch (e) { thrown = e.message === "boom"; }
check("throwing value", thrown && partial === undefined);

// Duplicate keys: the last one wins.
const dup = { k: 1, k2: 0, k: 2 };
check("duplicate keys", dup.k === 2 && sortedKeys(dup) === "k,k2");

// Numeric and quoted keys.
const num = { 1: "one", "two words": 2, 0: "zero" };
check("numeric keys", num[1] === "one" && num["0"] === "zero" && num["two words"] === 2);
check("numeric keys are not array-like", num.length === undefined);

// Forms the pass leaves to the general path, mixed with plain fields.
const base = { inherited: true };
const withProto = { a: 1, __proto__: base, b: 2 };
check("__proto__ in literal", withProto.inherited === true && withProto.a === 1 && withProto.b === 2
      && Object.getPrototypeOf(withProto) === base);
const src = { s1: 1, s2: 2 };
const spread = { a: 0, ...src, b: 3 };
check("spread", spread.a === 0 && spread.s1 === 1 && spread.s2 === 2 && spread.b === 3);
const key = "dyn";
const computed = { before: 1, [key + "1"]: "d", after: 2 };
check("computed key", computed.dyn1 === "d" && computed.before === 1 && computed.after === 2);
const methods = {
    v: 3,
    get double() { return this.v * 2; },
    set double(x) { this.v = x / 2; },
    m() { return this.v + 1; },
    w: 5,
};
check("accessor get", methods.double === 6);
methods.double = 20;
check("accessor set", methods.v === 10 && methods.m() === 11 && methods.w === 5);
const parent = { hi() { return "parent"; } };
const child = { __proto__: parent, z: 1, hi() { return "child>" + super.hi(); } };
check("super in method", child.hi() === "child>parent" && child.z === 1);
const shorthand = (() => { const p = 1, q = 2; return { p, q, r: p + q }; })();
check("shorthand", shorthand.p === 1 && shorthand.q === 2 && shorthand.r === 3);

// Literals built across an await and a yield.
function* gen() { const o = { a: 1, b: yield "first", c: 3 }; return o; }
const it = gen();
check("yield: first", it.next().value === "first");
const done = it.next(2);
check("yield: built object", done.done && done.value.a === 1 && done.value.b === 2 && done.value.c === 3);
let asyncOk = false;
(async () => {
    const o = { a: 1, b: await Promise.resolve(2), c: 3 };
    o.d = 4;
    asyncOk = o.a === 1 && o.b === 2 && o.c === 3 && o.d === 4;
})().then(() => {
    check("await inside literal", asyncOk);
    finish();
});

// JSON round trip and Object.assign onto a literal.
const j = { s: "x", n: 1.5, b: false, arr: [1, { k: "v" }], o: { deep: null } };
check("JSON", JSON.stringify(JSON.parse(JSON.stringify(j))) === JSON.stringify(j));
const target = { t: 1 };
Object.assign(target, { u: 2 });
check("Object.assign target", target.t === 1 && target.u === 2);

// Many objects: values stay distinct and correct.
let sum = 0;
const many = [];
for (let i = 0; i < 2000; i++) many.push({ id: i, half: i * 0.5, tag: "t" + (i % 7) });
for (const o of many) sum += o.id + o.half;
check("many objects", sum === 1999000 + 999500 && many[1234].tag === "t2");

function finish() {
    if (failures === 0) {
        console.log("object_literal_build: all " + checks + " checks passed");
    } else {
        console.log("object_literal_build: " + failures + " of " + checks + " checks FAILED");
        if (typeof process !== "undefined") process.exit(1);
    }
}
