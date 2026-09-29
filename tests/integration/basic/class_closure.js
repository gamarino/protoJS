// Class constructors are closures.
//
// ECMA-262 §15.7.14 ClassDefinitionEvaluation creates a fresh constructor
// function object each time a class definition is evaluated, closed over the
// running lexical environment. protoJS used to reuse the raw constant-pool
// function object as the constructor, so every evaluation of the same class
// definition returned the SAME object and the constructor could not see the
// enclosing function's variables.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, condition, detail) {
    if (!condition) {
        failures.push(name + (detail !== undefined ? " — " + detail : ""));
    }
}

function makeClass(x) {
    class A {
        constructor() { this.v = x; }
        get() { return x; }
    }
    return A;
}

var A1 = makeClass(1);
var A2 = makeClass(2);

check("each evaluation of a class definition yields a new constructor",
      A1 !== A2);
check("the constructor captures the enclosing variable",
      new A1().v === 1 && new A2().v === 2,
      "v = " + new A1().v + ", " + new A2().v);
check("methods capture the enclosing variable",
      new A1().get() === 1 && new A2().get() === 2);
A1.tag = "one";
check("a property set on one class does not leak into another",
      A2.tag === undefined, "A2.tag = " + A2.tag);
check("each class has its own prototype object",
      A1.prototype !== A2.prototype);
check("prototype.constructor points back at its own class",
      A1.prototype.constructor === A1 && A2.prototype.constructor === A2);

function makeExpr(y) {
    return class { constructor() { this.v = y; } };
}
check("class expressions capture the enclosing variable",
      new (makeExpr(7))().v === 7);

var counter = 0;
class Counted {
    constructor() { counter++; this.n = counter; }
}
check("a top-level class still constructs",
      new Counted().n === 1 && new Counted().n === 2);
check("a class keeps its name and length",
      Counted.name === "Counted" && A1.name === "A" && A1.length === 0);

var threw = false;
try { Counted(); } catch (err) { threw = err instanceof TypeError; }
check("calling a class without new throws TypeError", threw);

class Base { constructor(n) { this.n = n; } }
function makeDerived(k) {
    return class extends Base { constructor() { super(k); } };
}
check("a derived class closed over a variable reaches super()",
      new (makeDerived(5))().n === 5 && new (makeDerived(6))().n === 6);

// Instance fields are initialised exactly once per construction, by the
// constructor body itself: the class_fields_init closure the constructor
// captures. The interpreter used to run that closure a second time from its
// own dispatch sites, written when the constructor could not capture it.
var inits = 0;
class Implicit { f = ++inits; }
var imp = new Implicit();
check("fields of a class with an implicit constructor run once",
      inits === 1 && imp.f === 1, "initialiser ran " + inits + " time(s)");

inits = 0;
class Explicit { f = ++inits; constructor() { this.seen = this.f; } }
var exp = new Explicit();
check("fields of a class with an explicit constructor run once, before its body",
      inits === 1 && exp.seen === 1, "ran " + inits + ", body saw " + exp.seen);

var order = [];
class FieldBase { b = order.push("base field"); }
class FieldDerived extends FieldBase {
    d = order.push("derived field");
    constructor() { order.push("before super"); super(); order.push("after super"); }
}
new FieldDerived();
check("derived fields run once, after super() returns",
      order.join(",") === "before super,base field,derived field,after super",
      order.join(","));

function fieldCapture(v) { return class { f = v; }; }
check("a field initialiser captures the enclosing variable",
      new (fieldCapture(3))().f === 3 && new (fieldCapture(4))().f === 4);

// An own property's descriptor is never inherited. A class constructor has
// Function.prototype on its chain, whose own `constructor` is non-enumerable;
// reading that descriptor through the chain hid the class's own, enumerable
// `constructor` from propertyIsEnumerable, Object.keys and for-in.
class WithStaticCtor { static ['constructor'] = 42; }
var forInKeys = [];
for (var fk in WithStaticCtor) forInKeys.push(fk);
check("an own static field named constructor is enumerable",
      WithStaticCtor.propertyIsEnumerable('constructor') &&
      Object.keys(WithStaticCtor).join(",") === "constructor" &&
      forInKeys.join(",") === "constructor",
      "keys = " + Object.keys(WithStaticCtor).join(",") + ", for-in = " + forInKeys.join(","));
function PlainFn() {}
PlainFn.constructor = 5;
check("an own property of a function is not hidden by Function.prototype's descriptor",
      PlainFn.propertyIsEnumerable('constructor') && Object.keys(PlainFn).join(",") === "constructor",
      "keys = " + Object.keys(PlainFn).join(","));

if (failures.length) {
    console.log("class_closure: " + failures.length + " check(s) failed");
    for (var f = 0; f < failures.length; f++) {
        console.log("  FAIL: " + failures[f]);
    }
    process.exit(1);
}
console.log("class_closure: all checks passed");
