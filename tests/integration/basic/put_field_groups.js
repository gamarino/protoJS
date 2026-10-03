// Runs of writes to one object are published as one version
// (markPutFieldGroups in src/runtime/BytecodeSpecialiser.cpp, "Write groups"
// in src/runtime/ProtoInterpreter.cpp).  The rewrite must be invisible: every
// case below has the result the per-write path gives, including the cases
// where the runtime must refuse the group -- a setter, a frozen object, a
// Proxy, an array, an operand whose valueOf observes the object -- and the
// cases that throw in the middle of a run.  The file passes unchanged under
// Node.js.

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
function sloppy(src) { return Function(src); }  // a non-strict function body

// A constructor with several fields, constants, arguments and arithmetic.
class Point5 {
    constructor(i, name) {
        this.id = i;
        this.name = name;
        this.qty = i + 1;
        this.price = i * 0.5;
        this.flag = i > 2;
    }
}
const p5 = new Point5(4, "n4");
check("constructor fields",
      p5.id === 4 && p5.name === "n4" && p5.qty === 5 && p5.price === 2 && p5.flag === true);
check("constructor keys", Object.keys(p5).sort().join() === "flag,id,name,price,qty");
p5.qty = 9;
const q5 = new Point5(1, "n1");
check("instances are independent", p5.qty === 9 && q5.qty === 2 && q5.flag === false);

// A method updating this.x / this.y from its own fields.
class Vec {
    constructor(x, y) { this.x = x; this.y = y; }
    move(dx, dy) { this.x = this.x + dx; this.y = this.y + dy; }
    swap() { const t = this.x; this.x = this.y; this.y = t; }
}
const v = new Vec(1, 2);
v.move(3, 4);
check("method this.x / this.y", v.x === 4 && v.y === 6);
v.swap();
check("swap through a local", v.x === 6 && v.y === 4);

// Compound assignment on a plain object (`p.x += 1; p.y += 1`).
function bump(p) { p.x += 1; p.y += 1; }
const pb = { x: 1, y: 10 };
for (let i = 0; i < 5; i++) bump(pb);
check("p.x += 1; p.y += 1", pb.x === 6 && pb.y === 15);

// A later statement reads a field an earlier one wrote: it must see the
// new value.
function chain(o) { o.a = 1; o.b = o.a + 1; o.c = o.b + 1; }
const ch = {};
chain(ch);
check("read after write in the run", ch.a === 1 && ch.b === 2 && ch.c === 3);

// The same name twice: the later write wins.
function twice(o) { o.k = 1; o.j = 2; o.k = 3; }
const tw = {};
twice(tw);
check("repeated name", tw.k === 3 && tw.j === 2);

// A setter on the prototype runs, in order, and sees the earlier writes.
const log = [];
class WithSetter {
    set b(v) { log.push("set b=" + v + " a=" + this.a); this._b = v; }
    get b() { return this._b; }
}
function fill(o) { o.a = 1; o.b = 2; o.c = 3; }
const ws = new WithSetter();
fill(ws);
check("setter called in order", log.join() === "set b=2 a=1", log.join());
check("setter receiver and fields", ws.a === 1 && ws.b === 2 && ws.c === 3 && ws._b === 2);

// A setter installed on Object.prototype after the code was loaded.
const seen = [];
Object.defineProperty(Object.prototype, "zz", {
    set(v) { seen.push(v + ":" + this.yy); }, configurable: true,
});
function fillZ(o) { o.yy = 1; o.zz = 2; o.ww = 3; }
const fz = {};
fillZ(fz);
check("inherited setter added later", seen.join() === "2:1" && fz.yy === 1 && fz.ww === 3
      && !Object.prototype.hasOwnProperty.call(fz, "zz"), seen.join());
delete Object.prototype.zz;

// A getter whose read observes the earlier writes of the run.
let observed;
const watcher = { get peek() { observed = this.target.a; return 7; } };
function withGetter(o, w) { o.a = 5; o.b = w.peek; o.c = 1; }
const tg = { a: 0 };
watcher.target = tg;
withGetter(tg, watcher);
check("getter sees earlier writes", observed === 5 && tg.b === 7 && tg.c === 1, observed);

// An operand whose valueOf observes the receiver: the run must not defer
// the earlier writes past it.
let valueOfSaw;
function withValueOf(o, x) { o.a = 1; o.b = x + 1; o.c = 2; }
const vo = {};
withValueOf(vo, { valueOf() { valueOfSaw = vo.a; return 10; } });
check("valueOf sees earlier writes", valueOfSaw === 1 && vo.b === 11 && vo.c === 2, valueOfSaw);

// A field read by the run whose own getter observes the receiver.
let getterSaw;
const gr = { a: 0, get g() { getterSaw = this.a; return 3; } };
function readsGetter(o) { o.a = 4; o.b = o.g + 1; }
readsGetter(gr);
check("own getter in the run", getterSaw === 4 && gr.b === 4, getterSaw);

// Frozen receiver: TypeError at the first write in strict code, every write
// ignored in sloppy code.
function strictFill(o) { o.a = 1; o.b = 2; }
const fr = Object.freeze({ a: 0, b: 0 });
let err;
try { strictFill(fr); } catch (e) { err = e; }
check("frozen, strict: TypeError", err instanceof TypeError && fr.a === 0 && fr.b === 0);
const sloppyFill = sloppy("var o = arguments[0]; o.a = 1; o.b = 2; return o;");
sloppyFill(fr);
check("frozen, sloppy: ignored", fr.a === 0 && fr.b === 0);

// Non-extensible receiver: existing fields are written, a new one fails
// at its own write, after the earlier ones.
function addFields(o) { o.a = 1; o.n = 2; o.b = 3; }
const ne = Object.preventExtensions({ a: 0, b: 0 });
err = undefined;
try { addFields(ne); } catch (e) { err = e; }
check("non-extensible, strict", err instanceof TypeError && ne.a === 1 && ne.b === 0
      && !("n" in ne));

// A non-writable field in the middle of a run.
function fill3(o) { o.a = 1; o.ro = 2; o.c = 3; }
const nw = { a: 0, c: 0 };
Object.defineProperty(nw, "ro", { value: 9, writable: false, enumerable: true });
err = undefined;
try { fill3(nw); } catch (e) { err = e; }
check("non-writable, strict", err instanceof TypeError && nw.a === 1 && nw.ro === 9 && nw.c === 0);

// An exception in the middle of a run: the writes before it are visible,
// the ones after it are not.
function throwsMidRun(o, f) { o.a = 1; o.b = f(); o.c = 3; }
const ex = {};
err = undefined;
try { throwsMidRun(ex, () => { throw new Error("boom"); }); } catch (e) { err = e; }
check("exception mid-run", err && err.message === "boom" && ex.a === 1
      && !("b" in ex) && !("c" in ex));

// A TDZ read in the middle of a run.
function tdz(o) {
    o.a = 1;
    o.b = later;  // ReferenceError: `later` is in its TDZ
    let later = 2;
    o.c = later;
}
const tz = {};
err = undefined;
try { tdz(tz); } catch (e) { err = e; }
check("TDZ mid-run", err instanceof ReferenceError && tz.a === 1 && !("b" in tz));

// Arithmetic that throws (BigInt mixed with Number) in the middle of a run.
function mixes(o, big) { o.a = 1; o.b = big + 1; o.c = 3; }
const mx = {};
err = undefined;
try { mixes(mx, 10n); } catch (e) { err = e; }
check("BigInt mix mid-run", err instanceof TypeError && mx.a === 1 && !("b" in mx));

// A Proxy receiver: the set trap sees every write, in order.
const trapLog = [];
const target = {};
const px = new Proxy(target, {
    set(t, k, val, r) { trapLog.push(k + "=" + val); t[k] = val; return true; },
});
function fillP(o) { o.a = 1; o.b = 2; o.c = 3; }
fillP(px);
check("Proxy receiver", trapLog.join() === "a=1,b=2,c=3" && target.c === 3, trapLog.join());

// A Proxy on the prototype chain.
const chainLog = [];
const protoProxy = new Proxy({}, {
    set(t, k, val, r) { chainLog.push(k); Object.defineProperty(r, k, { value: val, writable: true, enumerable: true, configurable: true }); return true; },
});
const viaProxy = Object.create(protoProxy);
fillP(viaProxy);
check("Proxy on the chain", chainLog.join() === "a,b,c" && viaProxy.b === 2, chainLog.join());

// Array receiver: named fields and `length`.
function fillA(arr) { arr.first = 1; arr.second = 2; arr.length = 1; }
const arr = [1, 2, 3];
fillA(arr);
check("array receiver", arr.first === 1 && arr.second === 2 && arr.length === 1 && arr[1] === undefined);

// Typed array and arguments receivers.
const ta = new Uint8Array(2);
fillP(ta);
check("typed array receiver", ta.a === 1 && ta.c === 3 && ta.length === 2);
function argsReceiver() { const o = arguments; o.a = 1; o.b = 2; return o; }
const ar = argsReceiver(5);
check("arguments receiver", ar.a === 1 && ar.b === 2 && ar[0] === 5);

// A function receiver (static fields).
function Fn() {}
function statics(f) { f.count = 0; f.label = "fn"; }
statics(Fn);
check("function receiver", Fn.count === 0 && Fn.label === "fn");

// Primitive, undefined and global receivers: the runtime never groups writes
// on them, and every write takes the per-write path.  A primitive receiver
// fails at the first write (TypeError in strict code, nothing written); an
// undefined receiver throws TypeError; a sloppy function called without a
// receiver writes to the global object.
err = undefined;
try { fillP("abc"); } catch (e) { err = e; }
check("primitive receiver, strict", err instanceof TypeError && "abc".a === undefined);
err = undefined;
try { fillP(undefined); } catch (e) { err = e; }
check("undefined receiver", err instanceof TypeError);
sloppy("this.__pfgA = 1; this.__pfgB = 2;")();
check("global receiver", globalThis.__pfgA === 1 && globalThis.__pfgB === 2);
delete globalThis.__pfgA;
delete globalThis.__pfgB;

// A sealed receiver: existing fields can be written.
const se = Object.seal({ a: 0, b: 0 });
strictFill(se);
check("sealed receiver", se.a === 1 && se.b === 2);

// Strings and booleans as arithmetic operands.
function mixOps(o, s, b) { o.a = s + "!"; o.b = b && true; o.c = s < "z"; o.d = !b; }
const mo = {};
mixOps(mo, "abc", true);
check("string and boolean operands", mo.a === "abc!" && mo.b === true && mo.c === true && mo.d === false);

// Values that are objects (not arithmetic operands) are stored as they are.
function links(node, left, right) { node.left = left; node.right = right; node.size = 1; }
const l = {}, r = {}, nd = {};
links(nd, l, r);
check("object values", nd.left === l && nd.right === r && nd.size === 1);

// A loop: the run is checked on every iteration.
function loopFill(o, n) { for (let i = 0; i < n; i++) { o.i = i; o.sq = i * i; } }
const lp = {};
loopFill(lp, 10);
check("run in a loop", lp.i === 9 && lp.sq === 81);

if (failures) {
    console.log("put_field_groups: " + failures + " of " + checks + " checks failed");
    process.exit(1);
}
console.log("put_field_groups: all " + checks + " checks passed");
