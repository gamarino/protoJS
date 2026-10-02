// `new` and Reflect.construct on a function without [[Construct]] throw a
// TypeError (ECMA-262 §13.3.5.1.1 EvaluateNew step 5, §7.2.4 IsConstructor).
// Only an ordinary `function` and a class constructor are constructors;
// arrow functions, methods, accessors, generators and async functions are not.
// Before the fix every bytecode function was run as a constructor: `new
// (() => 1)()` returned an object. Values were checked against Node.js.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];
function throwsTypeError(name, fn) {
    try { fn(); failures.push(name + ': no exception'); }
    catch (e) { if (!(e instanceof TypeError)) failures.push(name + ': threw ' + e); }
}
function constructs(name, fn) {
    try { var r = fn(); if (typeof r !== 'object' || r === null) failures.push(name + ': got ' + r); }
    catch (e) { failures.push(name + ': threw ' + e); }
}

var arrow = () => 1;
var o = { m() { return 1; }, *g() {}, async am() {}, get x() { return 1; } };
throwsTypeError('new arrow', function () { return new arrow(); });
throwsTypeError('new async function', function () { return new (async function () {})(); });
throwsTypeError('new async arrow', function () { return new (async () => 1)(); });
throwsTypeError('new generator', function () { return new (function* () {})(); });
throwsTypeError('new async generator', function () { return new (async function* () {})(); });
throwsTypeError('new method', function () { return new o.m(); });
throwsTypeError('new generator method', function () { return new o.g(); });
throwsTypeError('new async method', function () { return new o.am(); });
throwsTypeError('new getter', function () {
    return new (Object.getOwnPropertyDescriptor(o, 'x').get)();
});
throwsTypeError('new class method', function () {
    class K { meth() {} static sm() {} }
    return new (new K().meth)();
});
throwsTypeError('new static method', function () { class K { static sm() {} } return new K.sm(); });
throwsTypeError('Reflect.construct arrow', function () { return Reflect.construct(arrow, []); });
throwsTypeError('Reflect.construct newTarget arrow', function () {
    return Reflect.construct(function () {}, [], arrow);
});
throwsTypeError('new bound arrow', function () { return new (arrow.bind(null))(); });

constructs('new function', function () { return new (function F() { this.a = 1; })(); });
constructs('new class', function () { class C {} return new C(); });
constructs('new derived class', function () { class B {} class D extends B {} return new D(); });
constructs('new Function()', function () { return new (new Function('this.z = 1'))(); });
constructs('Reflect.construct function', function () { return Reflect.construct(function () {}, []); });
constructs('new bound function', function () { return new ((function () {}).bind(null))(); });

// Array.from / Array.of on a non-constructor `this` fall back to a plain Array.
var fromArrow = Array.from.call(arrow, [1, 2]);
if (!Array.isArray(fromArrow) || fromArrow.length !== 2) failures.push('Array.from.call(arrow) = ' + fromArrow);

if (failures.length) {
    console.log('FAIL new_non_constructor:\n  ' + failures.join('\n  '));
    process.exit(1);
}
console.log('new_non_constructor: all checks passed');
