// A bound function is a constructor exactly when its target is one
// (§10.4.1.3), and constructing it constructs the target with the bound
// arguments first; a new.target that is the bound function itself becomes the
// target (§10.4.1.2). The bound this is ignored.
//
// Until 2026-10 Reflect.construct(bound, []) threw "Reflect.construct target
// is not a constructor": its IsConstructor check did not know bound functions.
// Asserting test: exits 1 on any failure.

var failures = [];
function check(name, got, expected) {
    if (!Object.is(got, expected)) failures.push(name + ": got " + String(got) + ", expected " + String(expected));
}
function throwsTypeError(name, f) {
    try { f(); failures.push(name + ": no exception"); }
    catch (e) { if (!(e instanceof TypeError)) failures.push(name + ": threw " + e); }
}
function attempt(name, f) {
    try { f(); } catch (e) { failures.push(name + ": threw " + e); }
}

function F(a, b) { this.a = a; this.b = b; this.nt = new.target; }
var BoundF = F.bind({ ignored: true }, 1);
function G() {}

attempt("Reflect.construct(bound)", function () {
    var o = Reflect.construct(BoundF, [2]);
    check("Reflect.construct(bound): bound args first", o.a, 1);
    check("Reflect.construct(bound): call args after", o.b, 2);
    check("Reflect.construct(bound): new.target is the target", o.nt, F);
    check("Reflect.construct(bound): prototype", Object.getPrototypeOf(o), F.prototype);
    check("Reflect.construct(bound): bound this ignored", o.ignored, undefined);
});
attempt("Reflect.construct(bound, [], G)", function () {
    var o = Reflect.construct(BoundF, [2], G);
    check("Reflect.construct(bound, [], G): new.target", o.nt, G);
    check("Reflect.construct(bound, [], G): prototype", Object.getPrototypeOf(o), G.prototype);
});
attempt("Reflect.construct(F, [], bound)", function () {
    var o = Reflect.construct(F, [5], BoundF);
    check("Reflect.construct(F, [], bound): new.target", o.nt, BoundF);
    check("Reflect.construct(F, [], bound): a", o.a, 5);
});
attempt("Reflect.construct on a doubly bound function", function () {
    var BB = BoundF.bind(null, 3);
    var o = Reflect.construct(BB, []);
    check("doubly bound: a", o.a, 1);
    check("doubly bound: b", o.b, 3);
    check("doubly bound: new.target", o.nt, F);
});

// new on a bound function.
var n = new BoundF(2);
check("new bound: a", n.a, 1);
check("new bound: b", n.b, 2);
check("new bound: new.target", n.nt, F);
check("new bound: instanceof", n instanceof F, true);
check("new bound: instanceof the bound function", n instanceof BoundF, true);

// A bound class.
class K { constructor(x) { this.x = x; this.nt = new.target; } }
var BoundK = K.bind(null, 7);
check("new bound class", new BoundK().x, 7);
check("new bound class: new.target", new BoundK().nt, K);
attempt("Reflect.construct(bound class)", function () {
    check("Reflect.construct(bound class)", Reflect.construct(BoundK, []).x, 7);
});
throwsTypeError("calling a bound class without new", function () { BoundK(); });

// class extends a bound function. A bound function has no prototype
// property; `extends` needs one (an object or null), so give it one, as
// test262's class/subclass/superclass-bound-function does.
throwsTypeError("class extends a bound function without a prototype", function () { class X extends BoundF {} });
BoundF.prototype = F.prototype;
class Sub extends BoundF {
    constructor(b) { super(b); this.sub = true; }
}
var s = new Sub(9);
check("extends bound: a", s.a, 1);
check("extends bound: b", s.b, 9);
check("extends bound: prototype", Object.getPrototypeOf(s), Sub.prototype);
check("extends bound: new.target", s.nt, Sub);
check("extends bound: own field", s.sub, true);
attempt("Reflect.construct(Sub)", function () {
    check("Reflect.construct of a class extending a bound function", Reflect.construct(Sub, [4]).b, 4);
});

// Not constructors: a bound arrow function, a bound method, a bound
// non-constructor built-in.
var arrow = (() => 1).bind(null);
var method = ({ m() {} }).m.bind(null);
throwsTypeError("new bound arrow", function () { new arrow(); });
throwsTypeError("new bound method", function () { new method(); });
throwsTypeError("Reflect.construct(bound arrow)", function () { Reflect.construct(arrow, []); });
throwsTypeError("Reflect.construct(bound method)", function () { Reflect.construct(method, []); });
throwsTypeError("Reflect.construct(F, [], bound arrow)", function () { Reflect.construct(F, [], arrow); });
throwsTypeError("class extends bound arrow", function () { class X extends arrow {} });

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("bound_constructors: all checks passed");
