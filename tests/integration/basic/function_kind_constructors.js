// The GeneratorFunction, AsyncFunction and AsyncGeneratorFunction intrinsics
// (ECMA-262 §27.3, §27.4, §27.7): each is the `constructor` of the prototype of
// functions of its kind, builds a function of that kind from source text
// (CreateDynamicFunction, §20.2.1.1.1), and its prototype object carries the
// right [[Prototype]] and @@toStringTag.  Generator objects inherit from their
// function's `prototype`, which inherits %GeneratorPrototype%.
//
// None of the three is a global binding (the variables below are named
// differently on purpose).  Passes under Node.js.

var failures = 0;
var checks = 0;
function check(name, cond, detail) {
    checks++;
    if (!cond) {
        failures++;
        console.log("FAILED: " + name + (detail !== undefined ? " -- " + detail : ""));
    }
}
function threw(fn, Ctor) {
    try { fn(); } catch (e) { return e instanceof Ctor; }
    return false;
}

var tag = Symbol.toStringTag;
var GenFn = Object.getPrototypeOf(function* () {}).constructor;
var AsyncFn = Object.getPrototypeOf(async function () {}).constructor;
var AsyncGenFn = Object.getPrototypeOf(async function* () {}).constructor;

// --- The constructors -------------------------------------------------------------
[[GenFn, "GeneratorFunction"],
 [AsyncFn, "AsyncFunction"],
 [AsyncGenFn, "AsyncGeneratorFunction"]].forEach(function (pair) {
    var C = pair[0], name = pair[1];
    check(name + " is a function", typeof C === "function");
    check(name + " is not Function", C !== Function);
    check(name + ".name", C.name === name, String(C.name));
    check(name + ".length", C.length === 1);
    check(name + " [[Prototype]] is Function", Object.getPrototypeOf(C) === Function);
    check(name + ".prototype [[Prototype]] is Function.prototype",
          Object.getPrototypeOf(C.prototype) === Function.prototype);
    check(name + ".prototype.constructor", C.prototype.constructor === C);
    check(name + ".prototype @@toStringTag", C.prototype[tag] === name, String(C.prototype[tag]));
    var pd = Object.getOwnPropertyDescriptor(C, "prototype");
    check(name + ".prototype descriptor", pd && !pd.writable && !pd.enumerable && !pd.configurable);
    check(name + " is not a global", !(name in globalThis));
});

// --- GenFn ------------------------------------------------------------
var g = GenFn("a", "yield a; yield a + 1;");
check("GeneratorFunction() builds a function", typeof g === "function");
check("its [[Prototype]]", Object.getPrototypeOf(g) === GenFn.prototype);
check("it yields", [...g(5)].join() === "5,6");
check("new GenFn()", [...new GenFn("yield 1")()].join() === "1");
check("GeneratorFunction() with no arguments", [...GenFn()()].length === 0);
check("its name is anonymous", g.name === "anonymous");
check("its source text", String(g) === "function* anonymous(a\n) {\nyield a; yield a + 1;\n}",
      JSON.stringify(String(g)));
check("syntax error is a SyntaxError", threw(function () { GenFn("yield +"); }, SyntaxError));
check("Object.prototype.toString of a generator function",
      Object.prototype.toString.call(g) === "[object GeneratorFunction]");

// %GeneratorPrototype% and generator objects.
var GeneratorPrototype = GenFn.prototype.prototype;
check("GeneratorFunction.prototype.prototype", typeof GeneratorPrototype === "object"
      && GeneratorPrototype[tag] === "Generator");
check("%GeneratorPrototype%.constructor", GeneratorPrototype.constructor === GenFn.prototype);
check("%GeneratorPrototype% has next/return/throw", typeof GeneratorPrototype.next === "function"
      && typeof GeneratorPrototype.return === "function" && typeof GeneratorPrototype.throw === "function");
function* declared() { yield 1; }
check("declared generator [[Prototype]]", Object.getPrototypeOf(declared) === GenFn.prototype);
check("fn.prototype inherits %GeneratorPrototype%",
      Object.getPrototypeOf(declared.prototype) === GeneratorPrototype);
check("fn.prototype has no constructor", !Object.prototype.hasOwnProperty.call(declared.prototype, "constructor"));
var it = declared();
check("generator object inherits fn.prototype", Object.getPrototypeOf(it) === declared.prototype);
check("generator object next is inherited", it.next === GeneratorPrototype.next);
check("generator object toString", Object.prototype.toString.call(it) === "[object Generator]");
check("generator object iterates", it.next().value === 1 && it.next().done === true);
var literal = { *method() { yield 7; } };
check("generator method keeps its own prototype",
      Object.prototype.hasOwnProperty.call(literal.method, "prototype")
      && Object.getPrototypeOf(literal.method.prototype) === GeneratorPrototype);
check("generator method's object", literal.method().next().value === 7
      && Object.getPrototypeOf(literal.method()) === literal.method.prototype);
var plain = { method() {} };
check("ordinary method has no prototype", !Object.prototype.hasOwnProperty.call(plain.method, "prototype"));
declared.prototype = null;
check("non-object fn.prototype falls back to %GeneratorPrototype%",
      Object.getPrototypeOf(declared()) === GeneratorPrototype);

// --- AsyncFn --------------------------------------------------------------
var af = AsyncFn("x", "return await x * 2;");
check("AsyncFunction() builds a function", typeof af === "function");
check("its [[Prototype]]", Object.getPrototypeOf(af) === AsyncFn.prototype);
check("it has no prototype property", !Object.prototype.hasOwnProperty.call(af, "prototype"));
check("Object.prototype.toString of an async function",
      Object.prototype.toString.call(af) === "[object AsyncFunction]");
var afResult = af(21);
check("it returns a promise", afResult instanceof Promise);
check("async function is not a constructor", threw(function () { new af(); }, TypeError));

// --- AsyncGenFn -----------------------------------------------------
var agf = new AsyncGenFn("yield 1; yield 2;");
check("AsyncGeneratorFunction() builds a function", typeof agf === "function");
check("its [[Prototype]]", Object.getPrototypeOf(agf) === AsyncGenFn.prototype);
var AsyncGeneratorPrototype = AsyncGenFn.prototype.prototype;
check("AsyncGeneratorFunction.prototype.prototype", AsyncGeneratorPrototype[tag] === "AsyncGenerator");
check("%AsyncGeneratorPrototype%.constructor",
      AsyncGeneratorPrototype.constructor === AsyncGenFn.prototype);
check("fn.prototype inherits %AsyncGeneratorPrototype%",
      Object.getPrototypeOf(agf.prototype) === AsyncGeneratorPrototype);
var agIt = agf();
check("async generator object inherits fn.prototype", Object.getPrototypeOf(agIt) === agf.prototype);

var pending = 2;
function finish() {
    if (--pending) return;
    if (failures) {
        console.log("function_kind_constructors: " + failures + " of " + checks + " checks failed");
        process.exit(1);
    }
    console.log("function_kind_constructors: all " + checks + " checks passed");
}
afResult.then(function (v) { check("async function result", v === 42, String(v)); finish(); },
              function (e) { check("async function result", false, String(e)); finish(); });
(async function () {
    var r = [];
    for await (var v of agIt) r.push(v);
    check("async generator yields", r.join() === "1,2", r.join());
})().then(finish, function (e) { check("async generator", false, String(e)); finish(); });
