// Classes: closures created in constructors, private members, the new.target
// of spread construction, and a bound method returned from a tail call.
//
// Before these fixes a closure created inside a class constructor threw
// "Class constructor cannot be invoked without 'new'" when called (the class
// flag was found through the closure's scope chain), `#x = 1` initialisers did
// nothing, private methods and accessors read undefined, each private write
// left a value on the operand stack, a foreign object's private field read
// undefined instead of throwing, `#x in o` aborted the function, and
// `return o.m()` with a bound m returned undefined.  The expected values are
// Node's.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, run, expected) {
    var actual;
    try {
        actual = run();
    } catch (e) {
        actual = "exception " + (e && e.name) + ": " + (e && e.message);
    }
    if (actual !== expected) {
        failures.push(name + " — got " + String(actual) + ", expected " + String(expected));
    }
}

function errorName(run) {
    try { run(); return "no exception"; } catch (e) { return e && e.name; }
}

// --- Closures created in a constructor ---------------------------------------

check("an arrow created in a constructor is callable", function () {
    class A { constructor() { const f = () => 1; this.v = f(); } }
    return new A().v;
}, 1);
check("a function expression created in a constructor is callable", function () {
    class A { constructor() { const f = function () { return 2; }; this.v = f(); } }
    return new A().v;
}, 2);
check("a closure kept from a constructor is callable later", function () {
    class A { constructor(x) { this.get = () => x; } }
    return new A(5).get();
}, 5);
check("an arrow created before super() sees this after it", function () {
    class B {}
    class A extends B { constructor() { const f = () => this; super(); this.ok = f() === this; } }
    return new A().ok;
}, true);
check("super() called twice through an arrow throws", function () {
    class B {}
    class A extends B { constructor() { const s = () => super(); s(); s(); } }
    return errorName(function () { new A(); });
}, "ReferenceError");
check("a class constructor called without new throws", function () {
    class A {}
    return errorName(function () { A(); });
}, "TypeError");
check("a class constructor called as a method throws", function () {
    class C {}
    var o = { C: C };
    return errorName(function () { o.C(); });
}, "TypeError");
check("a class constructor called through Function.prototype.call throws", function () {
    class C {}
    return errorName(function () { C.call({}); });
}, "TypeError");

// --- Private fields, methods and accessors -----------------------------------

check("a private field initialiser", function () {
    class A { #x = 1; get() { return this.#x; } }
    return new A().get();
}, 1);
check("a private field written in the constructor", function () {
    class A { #x; constructor(v) { this.#x = v; } get() { return this.#x; } }
    return new A(9).get();
}, 9);
check("private fields are per instance", function () {
    class A { #x; constructor(v) { this.#x = v; } get() { return this.#x; } }
    var a = new A(1), b = new A(2);
    return a.get() + b.get() * 10;
}, 21);
check("a private method", function () {
    class A { #m() { return 5; } run() { return this.#m(); } }
    return new A().run();
}, 5);
check("a private getter", function () {
    class A { get #g() { return 6; } run() { return this.#g; } }
    return new A().run();
}, 6);
check("a private setter", function () {
    class A { #v = 0; set #s(x) { this.#v = x; } run() { this.#s = 7; return this.#v; } }
    return new A().run();
}, 7);
check("private writes in a loop leave the stack balanced", function () {
    class A { #x = 0; run() { let i; for (i = 0; i < 100000; i++) { this.#x = i; } return this.#x; } }
    return new A().run();
}, 99999);
check("reading a private field of a foreign object throws", function () {
    class A { #x = 1; static r(o) { return o.#x; } }
    return errorName(function () { A.r({}); });
}, "TypeError");
check("writing a private field of a foreign object throws", function () {
    class A { #x = 1; static w(o) { o.#x = 2; } }
    return errorName(function () { A.w({}); });
}, "TypeError");
check("calling a private method on a foreign object throws", function () {
    class A { #m() { return 1; } static c(o) { return o.#m(); } }
    return errorName(function () { A.c({}); });
}, "TypeError");
check("#x in o", function () {
    class A { #x = 1; static has(o) { return #x in o; } }
    return A.has(new A()) + ":" + A.has({});
}, "true:false");
check("#m in o for a private method", function () {
    class A { #m() {} static has(o) { return #m in o; } }
    return A.has(new A()) + ":" + A.has({});
}, "true:false");
check("#x in a primitive throws", function () {
    class A { #x = 1; static has(o) { return #x in o; } }
    return errorName(function () { A.has(1); });
}, "TypeError");
check("a static private field", function () {
    class A { static #p = 3; static g() { return A.#p; } }
    return A.g();
}, 3);
check("a static private method", function () {
    class A { static #m() { return 4; } static g() { return A.#m(); } }
    return A.g();
}, 4);
check("private names are distinct per class evaluation", function () {
    function make() { return class { #x = 1; static read(o) { return o.#x; } }; }
    var A = make(), B = make();
    return errorName(function () { A.read(new B()); });
}, "TypeError");
check("a private field is not an own property", function () {
    class A { #x = 1; y = 2; }
    return Object.keys(new A()).join(",") + "|" + JSON.stringify(new A());
}, "y|{\"y\":2}");
check("a subclass instance has the base class's private fields", function () {
    class A { #x = 1; getX() { return this.#x; } }
    class B extends A { #y = 2; getY() { return this.#y; } }
    var b = new B();
    return b.getX() + b.getY();
}, 3);

// --- new.target of spread construction ---------------------------------------

check("new F(...[]) returns the new object", function () {
    function F() { this.v = 1; }
    return new F(...[]).v;
}, 1);
check("new G(...[]) inside a constructor gets G's prototype", function () {
    class G { who() { return "G"; } }
    class H { constructor() { this.g = new G(...[]); } }
    return new H().g.who();
}, "G");
check("super(...args) keeps the derived prototype", function () {
    class A { constructor(a, b) { this.s = a + b; } }
    class B extends A { constructor(...xs) { super(...xs); } who() { return "B"; } }
    var b = new B(1, 2);
    return b.s + b.who();
}, "3B");

// --- return o.m() with a bound m ----------------------------------------------

check("return o.m() with a bound function", function () {
    function f(a) { return a + 1; }
    var o = { m: f.bind(null, 41) };
    function h() { return o.m(); }
    return h();
}, 42);
check("return o.m(x) with a bound function and a call argument", function () {
    function f(a, b) { return a * b; }
    var o = { m: f.bind(null, 6) };
    function h(x) { return o.m(x); }
    return h(7);
}, 42);

if (failures.length) {
    console.log("class_members_and_calls: " + failures.length + " check(s) failed");
    for (var f = 0; f < failures.length; f++) {
        console.log("  FAIL: " + failures[f]);
    }
    process.exit(1);
}
console.log("class_members_and_calls: all checks passed");
