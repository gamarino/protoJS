// An ordinary function's `prototype` is created on first need
// (src/runtime/LazyPrototype.h), and that must not be observable: every way a
// program can look at the property sees it as if it had always been there.
// Each case below uses a fresh function, so the first observation is the one
// under test. Values were checked against Node.js.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];
function check(name, condition, detail) {
    if (!condition) failures.push(name + (detail !== undefined ? ' -- ' + detail : ''));
}

function f1() {}
check('hasOwnProperty first', f1.hasOwnProperty('prototype'));
check('identity stable', f1.prototype === f1.prototype);
check('constructor back-reference', f1.prototype.constructor === f1);
check('prototype inherits Object.prototype', Object.getPrototypeOf(f1.prototype) === Object.prototype);

function f2() {}
var d = Object.getOwnPropertyDescriptor(f2, 'prototype');
check('descriptor first: exists', d !== undefined);
check('descriptor: writable, not enumerable, not configurable',
      d && d.writable === true && d.enumerable === false && d.configurable === false,
      d && JSON.stringify([d.writable, d.enumerable, d.configurable]));
check('descriptor: value is the prototype', d && d.value === f2.prototype);
var cd = Object.getOwnPropertyDescriptor(f2.prototype, 'constructor');
check('constructor descriptor', cd && cd.value === f2 && cd.writable && !cd.enumerable && cd.configurable);

function f3() {}
check('getOwnPropertyNames first', Object.getOwnPropertyNames(f3).indexOf('prototype') >= 0,
      Object.getOwnPropertyNames(f3).join());
function f4() {}
check('Reflect.ownKeys first', Reflect.ownKeys(f4).indexOf('prototype') >= 0);
function f5() {}
check('in first', 'prototype' in f5);
function f6() {}
check('Reflect.has first', Reflect.has(f6, 'prototype'));
function f7() {}
check('Object.hasOwn first', Object.hasOwn(f7, 'prototype'));
function f8() {}
check('Object.keys omits prototype', Object.keys(f8).length === 0);
function f9() {}
check('getOwnPropertyDescriptors first', 'prototype' in Object.getOwnPropertyDescriptors(f9));
function f10() {}
check('computed key first', f10['proto' + 'type'] === f10.prototype && typeof f10.prototype === 'object');
function f11() {}
check('inherited lookup first', Object.create(f11).prototype === f11.prototype && f11.prototype !== undefined);
function f12() {}
check('Reflect.get first', typeof Reflect.get(f12, 'prototype') === 'object');

function C1() { this.v = 1; }
var c1 = new C1();
check('new first: instance of', c1 instanceof C1);
check('new first: [[Prototype]] is C1.prototype', Object.getPrototypeOf(c1) === C1.prototype);
check('new first: constructor', c1.constructor === C1);

function C2() {}
check('instanceof first, unrelated object', ({}) instanceof C2 === false);
check('instanceof first, prototype then exists', typeof C2.prototype === 'object');

function C3() {}
var r3 = Reflect.construct(C3, []);
check('Reflect.construct first', Object.getPrototypeOf(r3) === C3.prototype);

function Base() { this.b = 2; }
class Derived extends Base {}
var dv = new Derived();
check('class extends function first', dv instanceof Base && dv.b === 2
      && Object.getPrototypeOf(Derived.prototype) === Base.prototype);

function C4() {}
C4.prototype = { kind: 'assigned' };
check('assignment first', new C4().kind === 'assigned' && C4.prototype.kind === 'assigned');
var d4 = Object.getOwnPropertyDescriptor(C4, 'prototype');
check('assignment keeps the attributes', d4.writable && !d4.enumerable && !d4.configurable);

function C5() {}
Object.defineProperty(C5, 'prototype', { value: 5 });
check('defineProperty first', C5.prototype === 5);

function C6() {}
check('delete is refused', delete C6.prototype === false && typeof C6.prototype === 'object');
function C7() { 'use strict'; }
var threw = false;
try { (function () { 'use strict'; delete C7.prototype; })(); } catch (e) { threw = e instanceof TypeError; }
check('strict delete throws TypeError', threw);

function C8() {}
Object.freeze(C8);
check('freeze first: isFrozen', Object.isFrozen(C8));
var d8 = Object.getOwnPropertyDescriptor(C8, 'prototype');
check('freeze first: prototype frozen in place', d8 && d8.writable === false && typeof d8.value === 'object');

function C9() {}
Object.preventExtensions(C9);
check('preventExtensions first: prototype still there', C9.hasOwnProperty('prototype'));

function C10() {}
check('isSealed first is false', Object.isSealed(C10) === false);

var F = new Function('return 1');
check('Function constructor result', F.hasOwnProperty('prototype') && F.prototype.constructor === F);

// Functions without a prototype keep having none.
check('arrow has none', !(() => 1).hasOwnProperty('prototype'));
check('async has none', !(async function () {}).hasOwnProperty('prototype'));
check('method has none', !({ m() {} }).m.hasOwnProperty('prototype'));
check('bound has none', !(function () {}).bind(null).hasOwnProperty('prototype'));
var gen = function* () {};
check('generator has one, without constructor',
      gen.hasOwnProperty('prototype') && !gen.prototype.hasOwnProperty('constructor'));
class K {}
check('class prototype', K.prototype.constructor === K
      && Object.getOwnPropertyDescriptor(K, 'prototype').writable === false);

if (failures.length) {
    console.log('FAIL lazy_function_prototype:\n  ' + failures.join('\n  '));
    process.exit(1);
}
console.log('lazy_function_prototype: all checks passed');
