// Sync generators: exceptions, nesting, throw() and yield*.
//
// - An exception that leaves a generator's body is thrown by the next() call
//   that resumed it and completes the generator (ECMA-262 §27.5.3.3); protoJS
//   returned the thrown value as the iterator result.
// - A generator body that iterates another generator kept working after the
//   inner one yielded or finished; protoJS lost track of the outer generator
//   (the t_genIterator thread-local was cleared by the inner resume), so
//   `for (v of inner()) yield v` produced nothing.
// - throw(v) at a plain yield throws v there (§27.5.3.4); protoJS completed the
//   generator with v as its value. throw(v) before the first next(), or after
//   completion, throws v.
// - yield* delegates next/return/throw to the inner iterator and returns its
//   result objects as they are; protoJS's yield* ran a loop of its own on the
//   wrong operand and produced nothing or failed.
// Expected values were checked against Node.js.

let failures = 0;
function check(actual, expected, label) {
    const a = JSON.stringify(actual), e = JSON.stringify(expected);
    if (a !== e) { failures++; console.log("FAIL " + label + ": expected " + e + ", got " + a); }
}
function outcome(fn) {
    try { return { value: fn() }; } catch (e) { return { thrown: e instanceof Error ? e.message : e }; }
}

// Throw after a yield.
const g1 = (function* () { yield 1; throw { t: 1 }; })();
check(g1.next(), { value: 1, done: false }, "first next");
check(outcome(() => g1.next()), { thrown: { t: 1 } }, "throw after yield");
check(g1.next(), { done: true }, "completed after the throw");

// Throw before the first yield.
const g2 = (function* () { throw new Error("early"); })();
check(outcome(() => g2.next()), { thrown: "early" }, "throw before the first yield");
check(g2.next(), { done: true }, "completed after the early throw");

// A throw caught inside the body does not leave it.
const g3 = (function* () { try { throw 5; } catch (e) { yield e * 2; } })();
check(g3.next(), { value: 10, done: false }, "caught inside the body");

// generator.throw() into a body that does not catch it.
const g4 = (function* () { yield 1; yield 2; })();
g4.next();
check(outcome(() => g4.throw(new Error("injected"))), { thrown: "injected" }, "throw() uncaught");
check(g4.next(), { done: true }, "completed after throw()");

// for-of sees the exception.
check(outcome(() => { for (const x of (function* () { yield 1; throw "loop"; })()) {} }),
      { thrown: "loop" }, "for-of propagates");

// A nested generator's completion does not disturb the outer one.
function* inner() { yield "a"; }
const g5 = (function* () { for (const v of inner()) yield v; throw "outer"; })();
check(g5.next(), { value: "a", done: false }, "nested yield");
check(outcome(() => g5.next()), { thrown: "outer" }, "outer throw after nested completion");
check(g5.next(), { done: true }, "outer completed");

// throw() before the first next() and after completion.
const g6 = (function* () { yield 1; })();
check(outcome(() => g6.throw("early")), { thrown: "early" }, "throw() at the start");
check(g6.next(), { done: true }, "completed by throw() at the start");
check(outcome(() => g6.throw("late")), { thrown: "late" }, "throw() after completion");
check(g6.return(9), { value: 9, done: true }, "return() after completion");

// throw() caught inside the body at the yield.
const g7 = (function* () { try { yield 1; } catch (e) { yield "caught " + e; } })();
g7.next();
check(g7.throw("x"), { value: "caught x", done: false }, "throw() caught at the yield");

// yield*.
check([...(function* () { yield* [1, 2, 3]; yield 4; })()], [1, 2, 3, 4], "yield* an array");
function* innerRet() { yield "i1"; return "ret"; }
const log = [];
const g8 = (function* () { const r = yield* innerRet(); log.push(r); yield "after"; })();
check([...g8], ["i1", "after"], "yield* a generator");
check(log, ["ret"], "the value of a yield* expression");
const g9 = (function* () { const got = yield* (function* () { const x = yield "q"; return x * 2; })(); yield got; })();
g9.next();
check(g9.next(21), { value: 42, done: false }, "next(v) is forwarded through yield*");
const g10 = (function* () {
    try { yield* (function* () { try { yield "in"; } catch (e) { yield "inner caught " + e; } })(); }
    catch (e) { yield "outer caught " + e; }
})();
g10.next();
check(g10.throw("t"), { value: "inner caught t", done: false }, "throw() is forwarded through yield*");
let custom = 0;
const iterable = { [Symbol.iterator]() { return { next() { custom++; return custom > 2 ? { done: true, value: "end" } : { value: custom, done: false }; } }; } };
check([...(function* () { yield (yield* iterable); })()], [1, 2, "end"], "yield* a custom iterable");

if (failures) { console.log(failures + " failure(s)"); process.exit(1); }
console.log("generator_resumption: OK");
