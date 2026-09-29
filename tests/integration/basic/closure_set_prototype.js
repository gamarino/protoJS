// Changing a closure's [[Prototype]] must not detach it from its scope.
//
// A protoJS closure reaches its captured variables through its protoCore
// parent chain (the enclosing call's frame object and the global root), the
// same chain that carries its JS [[Prototype]]. setJSProtoOverride used to
// rebind that chain to [proto] alone, so after Object.setPrototypeOf(fn, obj),
// `fn.__proto__ = obj`, or `class D extends Base` (which sets D's
// [[Prototype]] to Base), the function silently read every captured variable
// as undefined.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, condition, detail) {
    if (!condition) {
        failures.push(name + (detail !== undefined ? " — " + detail : ""));
    }
}

function viaSetPrototypeOf(k) {
    function g() { return k; }
    Object.setPrototypeOf(g, { tag: "t" });
    return g;
}
var g1 = viaSetPrototypeOf(5);
check("Object.setPrototypeOf keeps captured variables", g1() === 5, "g() = " + g1());
check("Object.setPrototypeOf installs the new prototype",
      g1.tag === "t" && Object.getPrototypeOf(g1).tag === "t");

function viaProtoAccessor(k) {
    var g = function () { return k; };
    g.__proto__ = { tag: "u" };
    return g;
}
var g2 = viaProtoAccessor(6);
check("__proto__ assignment keeps captured variables", g2() === 6, "g() = " + g2());
check("__proto__ assignment installs the new prototype", g2.tag === "u");

function twice(k) {
    function g() { return k; }
    Object.setPrototypeOf(g, { first: 1 });
    Object.setPrototypeOf(g, { second: 2 });
    return g;
}
var g3 = twice(7);
check("a second setPrototypeOf keeps captured variables", g3() === 7, "g() = " + g3());
check("a second setPrototypeOf replaces the first prototype",
      g3.second === 2 && g3.first === undefined, "first = " + g3.first);

class Base {
    constructor(n) { this.n = n; }
    static make() { return "static from Base"; }
}
function derived(k) {
    return class extends Base {
        constructor() { super(k); this.k = k; }
    };
}
var D = derived(8);
var d = new D();
check("a derived constructor passes a captured variable to super()",
      d.n === 8, "n = " + d.n);
check("a derived constructor reads a captured variable after super()",
      d.k === 8, "k = " + d.k);
check("a derived class still inherits static methods",
      D.make() === "static from Base");
check("a derived class has its base as [[Prototype]]",
      Object.getPrototypeOf(D) === Base);

if (failures.length) {
    console.log("closure_set_prototype: " + failures.length + " check(s) failed");
    for (var f = 0; f < failures.length; f++) {
        console.log("  FAIL: " + failures[f]);
    }
    process.exit(1);
}
console.log("closure_set_prototype: all checks passed");
