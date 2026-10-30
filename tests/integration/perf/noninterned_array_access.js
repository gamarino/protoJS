// Element reads and writes must not build per-index sidecar names.  Until
// 2026-10 every element read by an Array built-in (arrGet in
// src/ArrayPrototype.cpp) built "__get_<i>__" and "__set_<i>__" as heap
// strings and probed them, every `a[i] = v` built "__set_<i>__" and
// "__pd_<i>__", and every hole read "__get_<i>__": non-interned attribute
// names, each canonicalised against protoCore's symbol table.  Those
// sidecars exist only on objects flagged by src/runtime/IndexKeyFlags.h.
// Run by noninterned_names.cmake, which fails when the process passes more
// than a small constant number of such names; this script reads 20,000
// elements nine times and writes, copies and reads holes 20,000 times each.  Results are checked here
// (exit 1 on a wrong one), so a fast wrong answer cannot pass.
"use strict";
const N = 20000;
const a = [];
for (let i = 0; i < N; i++) a.push(i);
const failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + got + ", expected " + expected);
}
const sumN = (N * (N - 1)) / 2;
check("reduce", a.reduce((s, x) => s + x, 0), sumN);
check("indexOf", a.indexOf(N - 1), N - 1);
check("includes", a.includes(-1), false);
let fe = 0;
a.forEach((x) => { fe += x; });
check("forEach", fe, sumN);
check("map", a.map((x) => x * 2)[N - 1], 2 * (N - 1));
check("filter", a.filter((x) => (x & 1) === 0).length, N / 2);
check("some", a.some((x) => x === N), false);
check("join", a.join(",").length, a.toString().length);
check("lastIndexOf", a.lastIndexOf(0), 0);
const w = [];
for (let i = 0; i < N; i++) w[i] = i * 3;
check("index writes", w[N - 1] + w.length, 3 * (N - 1) + N);
const filled = new Array(N).fill(1);
check("fill", filled.reduce((s, x) => s + x, 0), N);
const sparse = new Array(N);
let undef = 0;
for (let i = 0; i < N; i++) if (sparse[i] === undefined) undef++;
check("hole reads", undef, N);
// Accessors still take precedence over the element storage.
const b = [10, 20, 30];
Object.defineProperty(b, "1", { get() { return 99; } });
check("own getter", b.reduce((s, x) => s + x, 0), 139);
const holes = [1, , 3];
Object.defineProperty(Array.prototype, "1", { get() { return 7; }, configurable: true });
check("inherited getter on a hole", holes.reduce((s, x) => s + x, 0), 11);
delete Array.prototype[1];
let seen = 0;
const c = [1, 2, 3];
Object.defineProperty(c, "2", { set(v) { seen = v; }, get() { return 0; } });
c[2] = 42;
check("own setter on write", seen, 42);
const ro = [1, 2, 3];
Object.defineProperty(ro, "0", { writable: false });
let threw = false;
try { ro[0] = 9; } catch (e) { threw = e instanceof TypeError; }
check("non-writable index throws in strict code", threw, true);
check("non-writable index", ro[0], 1);
const lit = { get 0() { return "g"; }, length: 1 };
check("literal index getter", Array.prototype.join.call(lit), "g");
if (failures.length) {
    for (const f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("noninterned_array_access: " + 9 * N + " element reads and " + 3 * N + " element writes and hole reads, all results correct");
