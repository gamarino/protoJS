// new.target is undefined in a function that is called ([[Call]]) and the
// constructor's new.target in one that is constructed ([[Construct]], super(),
// Reflect.construct); an arrow function takes it from the enclosing function.
//
// Until 2026-10 a function called from native code (an Array callback,
// Function.prototype.call / apply, Reflect.apply, a bound function, a valueOf
// run by ToPrimitive, a generator resumed by next) inherited the new.target of
// whatever constructor was running: `new F()` where F runs
// `[1].forEach(function () { seen = new.target })` gave F.
// Asserting test: exits 1 on any failure.

var failures = [];
function check(name, got, expected) {
    if (got !== expected)
        failures.push(name + ": got " + (typeof got === "function" ? "function " + got.name : String(got))
            + ", expected " + (typeof expected === "function" ? "function " + expected.name : String(expected)));
}

// ---- [[Call]] from inside a constructor: undefined -----------------------

function ForEachCallback() {
    var seen = "unset";
    [1].forEach(function () { seen = new.target; });
    this.seen = seen;
}
check("Array.prototype.forEach callback", new ForEachCallback().seen, undefined);

function MapCallback() { this.seen = [1].map(function () { return new.target; })[0]; }
check("Array.prototype.map callback", new MapCallback().seen, undefined);

function SortComparator() {
    var seen = "unset";
    [2, 1].sort(function (a, b) { seen = new.target; return a - b; });
    this.seen = seen;
}
check("Array.prototype.sort comparator", new SortComparator().seen, undefined);

function DirectCall() { this.seen = (function () { return new.target; })(); }
check("direct call", new DirectCall().seen, undefined);

function MethodCall() { var o = { m: function () { return new.target; } }; this.seen = o.m(); }
check("method call", new MethodCall().seen, undefined);

function CallCall() { this.seen = (function () { return new.target; }).call(null); }
check("Function.prototype.call", new CallCall().seen, undefined);

function ApplyCall() { this.seen = (function () { return new.target; }).apply(null, []); }
check("Function.prototype.apply", new ApplyCall().seen, undefined);

function ReflectApply() { this.seen = Reflect.apply(function () { return new.target; }, null, []); }
check("Reflect.apply", new ReflectApply().seen, undefined);

function BoundCall() { this.seen = (function () { return new.target; }).bind(null)(); }
check("bound function call", new BoundCall().seen, undefined);

function ValueOfCall() {
    var seen = "unset";
    var o = { valueOf: function () { seen = new.target; return 1; } };
    void (o + 1);
    this.seen = seen;
}
check("valueOf run by ToPrimitive", new ValueOfCall().seen, undefined);

function ToStringCall() {
    var seen = "unset";
    var o = { toString: function () { seen = new.target; return "x"; } };
    void String(o);
    this.seen = seen;
}
check("toString run by String()", new ToStringCall().seen, undefined);

function* gen() { yield new.target; }
function GeneratorNext() { this.seen = gen().next().value; }
check("generator resumed by next()", new GeneratorNext().seen, undefined);

function GetterCall() {
    var o = { get g() { return new.target; } };
    this.seen = o.g;
}
check("getter", new GetterCall().seen, undefined);

class Derived0 {
    constructor() {
        var seen = "unset";
        [1].forEach(function () { seen = new.target; });
        this.seen = seen;
    }
}
class Derived1 extends Derived0 {
    constructor() {
        var before = "unset";
        [1].forEach(function () { before = new.target; });
        super();
        this.before = before;
    }
}
var d1 = new Derived1();
check("callback in a derived constructor before super()", d1.before, undefined);
check("callback in the base constructor run by super()", d1.seen, undefined);

// ---- Called without new: undefined ---------------------------------------

function Plain() { return new.target; }
check("plain call", Plain(), undefined);
check("plain call through call()", Plain.call(null), undefined);

// ---- [[Construct]]: the constructor, or the new.target given -------------

function F() { this.nt = new.target; }
check("new F", new F().nt, F);
check("new F(...spread)", new F(...[1, 2]).nt, F);
function G() {}
check("Reflect.construct(F, [])", Reflect.construct(F, []).nt, F);
check("Reflect.construct(F, [], G)", Reflect.construct(F, [], G).nt, G);
var BoundF = F.bind(null);
check("new on a bound function", new BoundF().nt, F);
check("Reflect.construct on a bound function", Reflect.construct(BoundF, []).nt, F);
check("Reflect.construct on a bound function, new.target G", Reflect.construct(BoundF, [], G).nt, G);

class A { constructor() { this.nt = new.target; } }
class B extends A { constructor() { super(); this.ntB = new.target; } }
class C extends B {}
class D extends B { constructor(...args) { super(...args); } }
check("class A", new A().nt, A);
check("super() from B", new B().nt, B);
check("B's own new.target", new B().ntB, B);
check("implicit constructor of C through B to A", new C().nt, C);
check("implicit constructor of C, B's new.target", new C().ntB, C);
check("super(...args) from D", new D(1).nt, D);
check("Reflect.construct(B, [], C)", Reflect.construct(B, [], C).nt, C);

function NestedNew() { this.inner = new F(); this.nt = new.target; }
var nn = new NestedNew();
check("new inside a constructor: the inner one", nn.inner.nt, F);
check("new inside a constructor: the outer one after it", nn.nt, NestedNew);

function NewInCallback() { this.inner = [1].map(function () { return new F(); })[0]; }
check("new inside a callback inside a constructor", new NewInCallback().inner.nt, F);

// ---- Arrow functions: the enclosing function's new.target ----------------

function ArrowDirect() { var a = () => new.target; this.seen = a(); }
check("arrow called directly", new ArrowDirect().seen, ArrowDirect);

function ArrowForEach() { var seen; [1].forEach(() => { seen = new.target; }); this.seen = seen; }
check("arrow as a forEach callback", new ArrowForEach().seen, ArrowForEach);

function ArrowCall() { this.seen = (() => new.target).call(null); }
check("arrow through call()", new ArrowCall().seen, ArrowCall);

function ArrowNested() { this.seen = (() => (() => new.target)())(); }
check("nested arrows", new ArrowNested().seen, ArrowNested);

function ArrowPlain() { return (() => new.target)(); }
check("arrow in a called function", ArrowPlain(), undefined);

class ArrowInClass extends A {
    constructor() { super(); var r; [1].forEach(() => { r = new.target; }); this.r = r; }
}
check("arrow in a derived class constructor", new ArrowInClass().r, ArrowInClass);

// A class constructor still needs new, wherever it is called from.
function CallsClass() {
    try { A(); this.r = "no error"; } catch (e) { this.r = e.constructor.name; }
    try { [1].forEach(A); this.r2 = "no error"; } catch (e) { this.r2 = e.constructor.name; }
}
var cc = new CallsClass();
check("class constructor called inside a constructor", cc.r, "TypeError");
check("class constructor as a callback inside a constructor", cc.r2, "TypeError");

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("new_target_calls: all checks passed");
