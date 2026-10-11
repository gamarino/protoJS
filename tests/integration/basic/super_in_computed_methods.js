// `super` inside a method, getter or setter whose key is computed resolves
// against the [[HomeObject]] the definition installs (ECMA-262
// MakeMethod, §10.2.7, called for every MethodDefinition whatever its key).
// protoJS gave a home object only to methods with a literal name, so
// `super.x` in `{ [k]() { return super.x; } }` or `get [k]()` found nothing.
// Expected values were checked against Node.js.

let failures = 0;
function check(actual, expected, label) {
    const a = JSON.stringify(actual), e = JSON.stringify(expected);
    if (a !== e) { failures++; console.log("FAIL " + label + ": expected " + e + ", got " + a); }
}

const k = "f" + "n";
const sym = Symbol("s");

// Object literals with an explicit prototype.
const base = {
    hello() { return "base " + this.tag; },
    get value() { return 10; },
    set value(v) { this.stored = v; },
};
const lit = {
    __proto__: base,
    tag: "lit",
    [k]() { return super.hello(); },
    get [k + "Get"]() { return super.value + 1; },
    set [k + "Set"](v) { super.value = v * 2; },
    [sym]() { return super.hello() + "!"; },
};
check(lit.fn(), "base lit", "literal computed method");
check(lit.fnGet, 11, "literal computed getter");
lit.fnSet = 5;
check(lit.stored, 10, "literal computed setter");
check(lit[sym](), "base lit!", "literal computed symbol method");

// Classes: prototype and static members.
class A {
    greet() { return "A"; }
    get prop() { return "pa"; }
    static make() { return "static A"; }
    static get sprop() { return "sa"; }
}
class B extends A {
    [k]() { return super.greet() + "B"; }
    get [k + "Get"]() { return super.prop + "b"; }
    set [k + "Set"](v) { this.last = super.greet() + v; }
    static [k]() { return super.make() + " via B"; }
    static get [k + "Get"]() { return super.sprop + "b"; }
    [sym]() { return super.greet() + "sym"; }
}
const b = new B();
check(b.fn(), "AB", "class computed method");
check(b.fnGet, "pab", "class computed getter");
b.fnSet = "x";
check(b.last, "Ax", "class computed setter");
check(B.fn(), "static A via B", "class static computed method");
check(B.fnGet, "sab", "class static computed getter");
check(b[sym](), "Asym", "class computed symbol method");

// The home object is the object the method was defined on, not the receiver.
const other = { __proto__: { hello() { return "other"; } }, tag: "o" };
other.borrowed = lit.fn;
check(other.borrowed(), "base o", "home object is fixed at definition");

if (failures) { console.log(failures + " failure(s)"); process.exit(1); }
console.log("super_in_computed_methods: OK");
