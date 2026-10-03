// Writes to a property of a value that is not an object (ECMA-262 §6.2.5.6
// PutValue, §10.1.9.2 OrdinarySetWithOwnDescriptor).
//
//   - undefined / null: TypeError in strict and sloppy code alike.
//   - string, number, boolean, Symbol, BigInt: the write runs on a transient
//     wrapper with the primitive as receiver.  A setter on the prototype chain
//     is called with the primitive as `this`; otherwise the write fails --
//     TypeError in strict code, ignored in sloppy code -- and the primitive
//     never grows a property.
//
// Every form of write is covered: `p.k = v` (OP_put_field), `p[k] = v`
// (OP_put_array_el), compound assignment, increment, destructuring
// assignment, and a run of writes to one receiver (the write groups of
// markPutFieldGroups).  See putOnNonObjectBase in
// src/runtime/ProtoInterpreter.cpp.  The file passes unchanged under Node.js.

"use strict";

let failures = 0;
let checks = 0;
function check(name, cond, detail) {
    checks++;
    if (!cond) {
        failures++;
        console.log("FAILED: " + name + (detail !== undefined ? " -- " + detail : ""));
    }
}
function throwsType(name, f) {
    let err;
    try { f(); } catch (e) { err = e; }
    check(name + ": TypeError", err instanceof TypeError, err === undefined ? "no exception" : String(err));
}
function noThrow(name, f) {
    let err;
    try { f(); } catch (e) { err = e; }
    check(name + ": no exception", err === undefined, String(err));
}
const sloppy = (src) => Function(src);  // a non-strict function body

const primitives = [
    ["string", "abc"],
    ["number", 5],
    ["double", 2.5],
    ["boolean", true],
    ["symbol", Symbol("s")],
    ["well-known symbol", Symbol.iterator],
    ["bigint", 10n],
];

// --- Strict code: every write to a primitive throws ---------------------------
for (const [label, p] of primitives) {
    throwsType("strict " + label + ".foo = 1", () => { const q = p; q.foo = 1; });
    throwsType("strict " + label + "['foo'] = 1", () => { const q = p; const k = "foo"; q[k] = 1; });
    throwsType("strict " + label + "[7] = 1", () => { const q = p; q[7] = 1; });
    throwsType("strict " + label + ".foo += 1", () => { const q = p; q.foo += 1; });
    throwsType("strict " + label + ".foo++", () => { const q = p; q.foo++; });
    throwsType("strict [" + label + ".foo] = [1]", () => { const q = p; [q.foo] = [1]; });
    throwsType("strict ({a: " + label + ".foo} = {a: 1})", () => { const q = p; ({ a: q.foo } = { a: 1 }); });
    check("strict " + label + " has no foo", p.foo === undefined);
}
// The own properties of a string are non-writable too.
throwsType("strict 'abc'[0] = 'z'", () => { const s = "abc"; s[0] = "z"; });
throwsType("strict 'abc'.length = 1", () => { const s = "abc"; s.length = 1; });

// --- undefined and null throw in both modes ------------------------------------
for (const [label, base] of [["undefined", undefined], ["null", null]]) {
    throwsType("strict " + label + ".foo = 1", () => { const q = base; q.foo = 1; });
    throwsType("strict " + label + "[k] = 1", () => { const q = base; const k = "foo"; q[k] = 1; });
    throwsType("strict " + label + "[0] = 1", () => { const q = base; q[0] = 1; });
}
const sloppyDot = sloppy("var o = arguments[0]; o.foo = 1; return 'done';");
const sloppyIdx = sloppy("var o = arguments[0]; o['foo'] = 1; return 'done';");
const sloppyNum = sloppy("var o = arguments[0]; o[3] = 1; return 'done';");
for (const [label, base] of [["undefined", undefined], ["null", null]]) {
    throwsType("sloppy " + label + ".foo = 1", () => sloppyDot(base));
    throwsType("sloppy " + label + "['foo'] = 1", () => sloppyIdx(base));
    throwsType("sloppy " + label + "[3] = 1", () => sloppyNum(base));
}
// The key of `null[key] = v` is not converted: the TypeError comes first.
{
    let converted = false;
    const key = { toString() { converted = true; return "k"; } };
    throwsType("null[key] = 1", () => { const q = null; q[key] = 1; });
    check("null[key] does not convert the key", converted === false);
}

// --- Sloppy code: writes to primitives are ignored -----------------------------
for (const [label, p] of primitives) {
    let r;
    noThrow("sloppy " + label + ".foo = 1", () => { r = sloppyDot(p); });
    noThrow("sloppy " + label + "['foo'] = 1", () => sloppyIdx(p));
    noThrow("sloppy " + label + "[3] = 1", () => sloppyNum(p));
    check("sloppy " + label + " completes", r === "done");
    // protoJS carries a well-known Symbol as the string "Symbol.<name>", and
    // an index read on it reads that string (a known deviation of reads,
    // not of writes), so the index check is skipped for it.
    check("sloppy " + label + " has no foo", p.foo === undefined
          && (label === "well-known symbol" || p[3] === undefined));
}

// --- A setter on the prototype chain runs, with the primitive as `this` --------
{
    const seen = [];
    Object.defineProperty(String.prototype, "tagged", {
        set(v) { "use strict"; seen.push(typeof this + ":" + this + "=" + v); },
        configurable: true,
    });
    Object.defineProperty(Number.prototype, "tagged", {
        set(v) { "use strict"; seen.push(typeof this + ":" + this + "=" + v); },
        configurable: true,
    });
    noThrow("strict string setter", () => { const s = "ab"; s.tagged = 1; });
    noThrow("strict number setter", () => { const n = 4; n["tagged"] = 2; });
    sloppy("var s = 'cd'; s.tagged = 3;")();
    check("setters called with the primitive receiver",
          seen.join() === "string:ab=1,number:4=2,string:cd=3", seen.join());
    // A getter without a setter: the write fails.
    Object.defineProperty(String.prototype, "readOnly", { get() { return 1; }, configurable: true });
    throwsType("strict write to a getter-only accessor", () => { const s = "x"; s.readOnly = 2; });
    delete String.prototype.tagged;
    delete Number.prototype.tagged;
    delete String.prototype.readOnly;
}
// A setter that throws: its exception propagates.
{
    Object.defineProperty(Boolean.prototype, "boom", {
        set(v) { throw new RangeError("from setter"); }, configurable: true,
    });
    let err;
    try { const b = false; b.boom = 1; } catch (e) { err = e; }
    check("exception from a setter on the chain", err instanceof RangeError, String(err));
    delete Boolean.prototype.boom;
}

// --- Wrapper objects are objects: writes to them succeed -----------------------
{
    const ws = new String("ab"), wn = new Number(1), wb = new Boolean(false), wy = Object(Symbol());
    ws.foo = 1; wn.foo = 2; wb.foo = 3; wy.foo = 4;
    check("wrapper objects accept writes", ws.foo === 1 && wn.foo === 2 && wb.foo === 3 && wy.foo === 4);
}

// --- A run of writes to one primitive receiver ----------------------------------
// markPutFieldGroups groups `o.a = ...; o.b = ...` but the runtime must refuse
// the group for a primitive receiver: the first write throws (strict) and the
// second never runs; in sloppy code both are ignored.
function fillRun(o) { o.a = 1; o.b = 2; o.c = 3; }
for (const [label, p] of primitives) {
    throwsType("strict run on " + label, () => fillRun(p));
    check("strict run on " + label + " wrote nothing", p.a === undefined && p.b === undefined);
}
throwsType("strict run on undefined", () => fillRun(undefined));
throwsType("strict run on null", () => fillRun(null));
const sloppyRun = sloppy("var o = arguments[0]; o.a = 1; o.b = 2; o.c = 3; return 'done';");
for (const [label, p] of primitives) {
    let r;
    noThrow("sloppy run on " + label, () => { r = sloppyRun(p); });
    check("sloppy run on " + label + " wrote nothing", r === "done" && p.a === undefined);
}
throwsType("sloppy run on undefined", () => sloppyRun(undefined));

if (failures) {
    console.log("primitive_property_writes: " + failures + " of " + checks + " checks failed");
    process.exit(1);
}
console.log("primitive_property_writes: all " + checks + " checks passed");
