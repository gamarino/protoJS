// The `this` of a function called without a receiver (ECMA-262 §10.2.1.2
// OrdinaryCallBindThis): a non-strict function sees the global object for
// undefined / null and a wrapper object for a primitive; a strict function
// sees the value it was given.
//
// The global object is the object `globalThis` and the top-level `this`
// denote.  protoJS keeps the root script's top-level bindings on a child of
// it (the module-scope split in runBytecode), and that child must never leak
// as `this`.  Functions built by the Function constructor are the case that
// matters most: Test262's harness obtains the global object as
// `Function("return this;")()`.
//
// The file is sloppy on purpose and passes unchanged under Node.js.

var failures = 0;
var checks = 0;
function check(name, cond, detail) {
    checks++;
    if (!cond) {
        failures++;
        console.log("FAILED: " + name + (detail !== undefined ? " -- " + detail : ""));
    }
}

var G = globalThis;

// --- Function() / new Function() ------------------------------------------------
var returnThis = Function("return this;");
check("Function() called bare", returnThis() === G);
check("new Function() called bare", new Function("return this;")() === G);
check("Function() via call(undefined)", returnThis.call(undefined) === G);
check("Function() via call(null)", returnThis.call(null) === G);
check("Function() via apply(null)", returnThis.apply(null, []) === G);
check("Function() via bind(undefined)", returnThis.bind(undefined)() === G);
check("Function() via bind(null)", returnThis.bind(null)() === G);
check("Function() as a callback", [1].map(returnThis)[0] === G);

// Writes through `this` land on the global object.
Function("this.__sloppyThisA = 1; this.__sloppyThisB = 2;")();
check("Function() writes reach globalThis", G.__sloppyThisA === 1 && G.__sloppyThisB === 2);
delete G.__sloppyThisA;
delete G.__sloppyThisB;

// Functions and arrows nested in a Function() body.
check("function nested in Function()", Function("return function () { return this; };")()() === G);
check("arrow nested in Function()", Function("return () => this;")()() === G);

// A strict Function() body keeps the value it was given.
var strictThis = Function("'use strict'; return this;");
check("strict Function() bare", strictThis() === undefined);
check("strict Function() call(null)", strictThis.call(null) === null);
check("strict Function() call(5)", strictThis.call(5) === 5);

// --- Declared functions, arrows, methods ----------------------------------------
function declared() { return this; }
function makeArrow() { return () => this; }
function strictDeclared() { "use strict"; return this; }
check("sloppy declaration called bare", declared() === G);
check("arrow in a sloppy function called bare", makeArrow()() === G);
check("strict declaration called bare", strictDeclared() === undefined);
check("strict declaration call(undefined)", strictDeclared.call(undefined) === undefined);
check("strict declaration apply(null)", strictDeclared.apply(null) === null);
check("strict declaration bind(null)", strictDeclared.bind(null)() === null);
var o = { m: function () { return this; } };
check("method keeps its receiver", o.m() === o);

// --- Primitive this is boxed for sloppy functions -------------------------------
check("call(5) boxed", typeof returnThis.call(5) === "object" && returnThis.call(5) instanceof Number);
check("call('s') boxed", typeof returnThis.call("s") === "object" && returnThis.call("s") instanceof String);
check("call(true) boxed", typeof returnThis.call(true) === "object" && returnThis.call(true) instanceof Boolean);
check("bind(7) boxed", typeof returnThis.bind(7)() === "object" && returnThis.bind(7)() instanceof Number);
check("bind(7) value", returnThis.bind(7)().valueOf() === 7);
check("strict bind(7) not boxed", strictDeclared.bind(7)() === 7);

if (failures) {
    console.log("sloppy_this_binding: " + failures + " of " + checks + " checks failed");
    process.exit(1);
}
console.log("sloppy_this_binding: all " + checks + " checks passed");
