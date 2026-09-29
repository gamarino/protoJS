// Every closure keeps its own bindings.
//
// protoJS used to publish each captured variable on the enclosing call's
// single frame object under its source name, and to resolve it again by name,
// starting at the function object, whenever the closure was called. One frame
// object per call cannot represent JavaScript's block scopes or per-iteration
// bindings, so same-named bindings overwrote each other, loop closures all saw
// the last iteration, and a function's own `length` or `prototype` shadowed a
// captured variable of that name. A captured parameter was also copied into a
// fresh cell instead of being shared with the function that declares it.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, condition, detail) {
    if (!condition) {
        failures.push(name + (detail !== undefined ? " — " + detail : ""));
    }
}

// --- sibling block scopes ---------------------------------------------------

var siblings = [];
{ let a = 1; siblings.push(() => a); }
{ let a = 2; siblings.push(() => a); }
check("sibling blocks at top level keep their own binding",
      siblings[0]() === 1 && siblings[1]() === 2,
      siblings[0]() + "," + siblings[1]());

function siblingsInFunction() {
    var out = [];
    { let b = "x"; out.push(() => b); }
    { const b = "y"; out.push(() => b); }
    return out[0]() + out[1]();
}
check("sibling blocks inside a function keep their own binding",
      siblingsInFunction() === "xy", siblingsInFunction());

function shadowing() {
    let s = "outer";
    const outer = () => s;
    { let s = "inner"; var inner = () => s; }
    return outer() + "/" + inner();
}
check("a shadowing block binding does not replace the outer one",
      shadowing() === "outer/inner", shadowing());

// --- per-iteration bindings --------------------------------------------------

function values(fns) { return fns.map(function (f) { return f(); }).join(","); }

var forLet = [];
for (let i = 0; i < 3; i++) forLet.push(() => i);
check("for (let ...) gives each iteration its own binding",
      values(forLet) === "0,1,2", values(forLet));

var forOf = [];
for (const v of ["p", "q", "r"]) forOf.push(() => v);
check("for (const ... of) gives each iteration its own binding",
      values(forOf) === "p,q,r", values(forOf));

var forIn = [];
for (let k in { m: 1, n: 2 }) forIn.push(() => k);
check("for (let ... in) gives each iteration its own binding",
      values(forIn) === "m,n", values(forIn));

var whileBody = [], n = 0;
while (n < 3) { let w = n * 10; whileBody.push(() => w); n++; }
check("a let declared in a loop body is fresh on every iteration",
      values(whileBody) === "0,10,20", values(whileBody));

var counters = [];
for (let c = 0; c < 2; c++) {
    counters.push({ get: () => c, bump: () => { c += 100; } });
}
counters[0].bump();
check("closures of one iteration share that iteration's binding",
      counters[0].get() === 100 && counters[1].get() === 1,
      counters[0].get() + "," + counters[1].get());

// --- captured names that a function object also defines ----------------------

function capLength(length) { return () => length; }
function capName(name) { return () => name; }
function capPrototype() { let prototype = "p"; return function () { return prototype; }; }
function capCall() { let call = 42; return () => call; }
check("a captured variable named length is not the function's length",
      capLength(7)() === 7, String(capLength(7)()));
check("a captured variable named name is not the function's name",
      capName("alice")() === "alice", String(capName("alice")()));
check("a captured variable named prototype is not the function's prototype",
      capPrototype()() === "p", typeof capPrototype()());
check("a captured variable named call is not Function.prototype.call",
      capCall()() === 42, typeof capCall()());

// --- parameters are shared with the closures that capture them -----------------

function paramWrittenByParent(x) { const g = () => x; x = 2; return g(); }
function paramWrittenByClosure(x) { const inc = () => { x++; }; inc(); inc(); return x; }
function paramBetweenClosures(x) {
    const read = () => x, write = (v) => { x = v; };
    write(9);
    return read();
}
check("a closure sees its parent's later write to a parameter",
      paramWrittenByParent(1) === 2, String(paramWrittenByParent(1)));
check("the parent sees a closure's write to a parameter",
      paramWrittenByClosure(0) === 2, String(paramWrittenByClosure(0)));
check("two closures share one captured parameter",
      paramBetweenClosures(1) === 9, String(paramBetweenClosures(1)));

// --- nesting, hoisting, recursion ------------------------------------------------

function threeLevels(a) {
    let b = a + 1;
    return function () {
        let c = b + 1;
        return () => a + b + c;
    };
}
check("captures resolve through three levels of nesting",
      threeLevels(1)()() === 6, String(threeLevels(1)()()));

function hoistedBeforeLet() {
    function inner() { return x; }
    let x = "late";
    return inner();
}
check("a hoisted function sees a let initialised after it was created",
      hoistedBeforeLet() === "late", String(hoistedBeforeLet()));

function tdz() {
    function early() { return y; }
    var threw = false;
    try { early(); } catch (err) { threw = err instanceof ReferenceError; }
    let y = 1;
    return threw && early() === 1;
}
check("a captured let is in its temporal dead zone before initialisation", tdz());

function makeFact() {
    const fact = (k) => (k <= 1 ? 1 : k * fact(k - 1));
    return fact;
}
check("a closure can call itself through its own binding", makeFact()(5) === 120);

// --- generators and implicit calls -------------------------------------------------

function* genLoop() {
    for (let g = 0; g < 3; g++) yield () => g;
}
var fromGen = [];
for (const f of genLoop()) fromGen.push(f);
check("closures created across generator yields keep their iteration",
      values(fromGen) === "0,1,2", values(fromGen));

function valueOfClosure(v) { return { valueOf: () => v }; }
check("an implicit valueOf call sees the closure's captured variable",
      valueOfClosure(40) + 2 === 42, String(valueOfClosure(40) + 2));

if (failures.length) {
    console.log("closure_scopes: " + failures.length + " check(s) failed");
    for (var f = 0; f < failures.length; f++) {
        console.log("  FAIL: " + failures[f]);
    }
    process.exit(1);
}
console.log("closure_scopes: all checks passed");
