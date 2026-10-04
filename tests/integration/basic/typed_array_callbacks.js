// %TypedArray%.prototype methods that take a callback call JavaScript
// functions, with thisArg, and stop at the callback's exception. protoJS
// called only native functions: with a JavaScript callback forEach did nothing,
// map returned zeros, some/every/find/findIndex/reduce ignored it.
// Expected values were checked against Node.js.

let failures = 0;
function check(actual, expected, label) {
    const a = JSON.stringify(actual), e = JSON.stringify(expected);
    if (a !== e) { failures++; console.log("FAIL " + label + ": expected " + e + ", got " + a); }
}
function thrownName(fn) { try { fn(); return "no throw"; } catch (e) { return e && e.name ? e.name : String(e); } }

const a = new Int8Array([1, 2, 3]);
let sum = 0;
const seen = [];
a.forEach(function (x, i, arr) { sum += x; seen.push([i, arr === a, this.tag]); }, { tag: "t" });
check([sum, seen], [6, [[0, true, "t"], [1, true, "t"], [2, true, "t"]]], "forEach");
check(Array.from(a.map(x => x * 10)), [10, 20, 30], "map");
check(Array.from(a.map(x => String(x + 1))), [2, 3, 4], "map coerces results");
check([a.some(x => x > 2), a.some(x => x > 5)], [true, false], "some");
check([a.every(x => x > 0), a.every(x => x > 1)], [true, false], "every");
check([a.find(x => x > 1), a.find(x => x > 5)], [2, undefined], "find");
check([a.findIndex(x => x > 1), a.findIndex(x => x > 5)], [1, -1], "findIndex");
check(a.reduce((p, x) => p + x, 100), 106, "reduce with an initial value");
check(a.reduce((p, x) => p * x), 6, "reduce without one");
check(a.reduceRight((p, x) => p + String(x), ""), "321", "reduceRight");
check([a.some(x => "") , a.every(() => "yes")], [false, true], "callback results use ToBoolean");
check(new Float64Array([1.5, 2.5]).map(Math.floor).join(), "1,2", "native callback");

// The callback's exception stops the iteration and propagates.
let calls = 0;
check(thrownName(() => a.forEach(() => { calls++; throw new RangeError("stop"); })), "RangeError", "forEach throws");
check(calls, 1, "forEach stops at the exception");
check(thrownName(() => a.map(() => { throw new SyntaxError("m"); })), "SyntaxError", "map throws");
check(thrownName(() => a.reduce(() => { throw new EvalError("r"); })), "EvalError", "reduce throws");
check(thrownName(() => a.forEach(42)), "TypeError", "non-callable callback");

if (failures) { console.log(failures + " failure(s)"); process.exit(1); }
console.log("typed_array_callbacks: OK");
