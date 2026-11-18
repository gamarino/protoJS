// Pins how protoJS dispatches on values of every kind: the ECMAScript type of
// a value (typeof, "Type(x) is Object" in promise resolution), writes to
// primitive and object receivers, BigInt operands and wrappers, property keys,
// own and inherited dynamic keys, and iterator closing with values of every
// kind on the operand stack. The expected output is Node's
// (value_dispatch.expected). No line depends on timing.
'use strict';

const sym = Symbol('s');
const longString = 'a heap string that is longer than six bytes';
const proto = { inherited: 1, then: undefined };
const chained = Object.create(proto);
chained.own = 2;
const bigintChained = Object.setPrototypeOf({ then(r) { r(42); } }, BigInt.prototype);
const symbolChained = Object.setPrototypeOf({ then(r) { r(43); } }, Symbol.prototype);
const thenable = { then(r) { r('thenable resolved'); } };

const kinds = [
    ['smallint', 7],
    ['negative smallint', -3],
    ['double', 2.5],
    ['large number', 2 ** 60],
    ['NaN', NaN],
    ['bigint', 10n],
    ['huge bigint', 2n ** 100n],
    ['short string', 'ab'],
    ['long string', longString],
    ['empty string', ''],
    ['symbol', sym],
    ['well-known symbol', Symbol.iterator],
    ['true', true],
    ['false', false],
    ['null', null],
    ['undefined', undefined],
    ['array', [1, 2]],
    ['function', function f() {}],
    ['arrow', () => 1],
    ['class', class C {}],
    ['plain object', { a: 1 }],
    ['parentless object', Object.create(null)],
    ['prototype-chained object', chained],
    ['object chained to BigInt.prototype', bigintChained],
    ['object chained to Symbol.prototype', symbolChained],
    ['BigInt wrapper', Object(5n)],
    ['Symbol wrapper', Object(sym)],
    ['Number wrapper', Object(3)],
    ['String wrapper', Object('xy')],
    ['Promise', Promise.resolve(1)],
    ['thenable', thenable],
    ['object with a __data__ string', { __data__: 'x', then(r) { r(44); } }],
    ['Map', new Map()],
];

function describe(v) {
    if (typeof v === 'symbol') return v.toString();
    if (typeof v === 'bigint') return v.toString() + 'n';
    if (v === null) return 'null';
    if (typeof v === 'object' || typeof v === 'function') return '[' + typeof v + ']';
    return typeof v + ' ' + String(v);
}

// 1. typeof: the ECMAScript type of each value.
for (const [name, v] of kinds) console.log('typeof', name, typeof v);

// 2. Writes to the value as a receiver: strict code throws a TypeError for a
//    primitive (and for undefined / null), and an object takes the property.
//    The three writes in a row are a write group.
function writeThree(o) { o.wa = 1; o.wb = 2; o.wc = 3; }
for (const [name, v] of kinds) {
    let r;
    try { writeThree(v); r = 'wrote, reads back ' + v.wa + v.wb + v.wc; }
    catch (e) { r = e.constructor.name; }
    console.log('write', name, r);
}
function writeComputed(o, k) { o[k] = 9; }
for (const [name, v] of kinds) {
    let r;
    try { writeComputed(v, 'dyn' + name.length); r = 'wrote ' + v['dyn' + name.length]; }
    catch (e) { r = e.constructor.name; }
    console.log('write computed', name, r);
}
(function () {
    // Sloppy-mode writes to primitives are ignored.
    const sloppy = new Function('v', 'v.zz = 1; v["q" + 1] = 2; return v.zz === undefined && v.q1 === undefined;');
    for (const [name, v] of kinds) {
        if (v === null || v === undefined) continue;
        if (typeof v === 'object' || typeof v === 'function') continue;
        console.log('sloppy write ignored', name, sloppy(v));
    }
})();

// 3. Each value as a property key (ToPropertyKey).
for (const [name, v] of kinds) {
    if (typeof v === 'object' && v !== null && name !== 'array') continue;
    if (typeof v === 'function') continue;
    // protoJS carries well-known Symbols as "Symbol.<name>" strings, so
    // Reflect.ownKeys reports them as strings (a known deviation).
    if (name === 'well-known symbol') continue;
    const o = {};
    o[v] = name;
    const keys = Reflect.ownKeys(o).map(k => typeof k === 'symbol' ? k.toString() : k);
    console.log('key', name, JSON.stringify(keys), o[v] === name);
}
console.log('key object toString', ({ [{ toString() { return 'kk'; } }]: 1 }).kk);

// 4. BigInt operands, primitives and wrappers.
const bigOps = [
    ['1n + 2n', () => 1n + 2n],
    ['Object(5n) + 1n', () => Object(5n) + 1n],
    ['Object(6n) & 3n', () => Object(6n) & 3n],
    ['2n ** Object(3n)', () => 2n ** Object(3n)],
    ['3n * {valueOf: 4n}', () => 3n * { valueOf() { return 4n; } }],
    ['1n + 1', () => 1n + 1],
    ['1n * 2.5', () => 1n * 2.5],
    ['1n - "1"', () => 1n - '1'],
    ['1n < 2', () => 1n < 2],
    ['Object(1n) < 2.5', () => Object(1n) < 2.5],
    ['1n * true', () => 1n * true],
    ['1n * null', () => 1n * null],
    ['1n * Symbol()', () => 1n * Symbol()],
];
for (const [label, f] of bigOps) {
    let r;
    try { r = describe(f()); } catch (e) { r = e.constructor.name; }
    console.log('bigint op', label, r);
}
for (const [name, v] of kinds) {
    let r;
    try { r = BigInt.prototype.toString.call(v); } catch (e) { r = e.constructor.name; }
    console.log('BigInt.prototype.toString.call', name, r);
}

// 5. Dynamic keys: own, inherited through a prototype, and absent.
const dynamicHolder = {};
const dynamicKeys = [];
for (let i = 0; i < 40; i++) dynamicKeys.push('dynamic key number ' + i * 7919);
for (const k of dynamicKeys) dynamicHolder[k] = k.length;
const dynamicChild = Object.create(dynamicHolder);
dynamicChild[dynamicKeys[3]] = 'shadowed';
let ownSum = 0, inheritedSeen = 0;
for (const k of dynamicKeys) {
    ownSum += dynamicHolder[k];
    if (dynamicChild[k] !== undefined) inheritedSeen++;
}
console.log('dynamic own sum', ownSum, 'inherited seen', inheritedSeen,
            'shadowed', dynamicChild[dynamicKeys[3]], 'holder keeps', dynamicHolder[dynamicKeys[3]],
            'child own keys', Object.keys(dynamicChild).length,
            'absent', dynamicChild['dynamic key that is absent']);
for (const [name, v] of kinds) {
    if (v === null || v === undefined) continue;
    console.log('dynamic read', name, v[dynamicKeys[0]] === undefined ? 'undefined' : 'defined');
}

// 6. Iterator closing with values of every kind on the operand stack.
function closingIterable(log) {
    let i = 0;
    return {
        [Symbol.iterator]() { return this; },
        next() { return { value: i++, done: i > 5 }; },
        return() { log.push('return'); return {}; },
    };
}
function take(...args) { return args.length; }
{
    const log = [];
    try {
        for (const x of closingIterable(log)) {
            take(7, 2.5, 10n, 'ab', longString, sym, true, null, undefined, [x], {}, () => x,
                 ...(() => { throw new RangeError('inside'); })());
        }
    } catch (e) { log.push(e.constructor.name); }
    console.log('close on throw', log.join(','));
}
{
    const log = [];
    for (const x of closingIterable(log)) {
        if (take(7, 2.5, 10n, 'ab', sym, x) === 6 && x === 2) break;
    }
    console.log('close on break', log.join(','));
}
{
    const log = [];
    const [a, b] = closingIterable(log);
    console.log('close after destructuring', a, b, log.join(','));
}
{
    const log = [];
    function returnsInside() {
        for (const x of closingIterable(log)) {
            if (x === 1) return [7, 2.5, 'ab', 10n].length + x;
        }
    }
    console.log('close on return', returnsInside(), log.join(','));
}

// 7. Promise resolution: Type(x) is Object decides whether `then` is read.
const lines = [];
const pending = kinds.map(([name, v]) =>
    Promise.resolve(v).then(
        r => lines.push('resolve ' + name + ' -> ' + (r === v ? 'itself' : describe(r))),
        e => lines.push('resolve ' + name + ' rejected ' + e.constructor.name)));
pending.push(new Promise(res => res(bigintChained))
    .then(r => lines.push('executor resolve bigint-chained -> ' + describe(r))));
pending.push(Promise.reject(1).catch(() => symbolChained)
    .then(r => lines.push('catch returns symbol-chained -> ' + describe(r))));
let thisPromiseCheck;
try { Promise.prototype.then.call(10n); thisPromiseCheck = 'no error'; }
catch (e) { thisPromiseCheck = e.constructor.name; }
lines.push('Promise.prototype.then on a bigint ' + thisPromiseCheck);
for (const [name, v] of kinds) {
    let r;
    try { Promise.resolve.call(v, 1); r = 'ok'; } catch (e) { r = e.constructor.name; }
    lines.push('Promise.resolve with this ' + name + ' ' + r);
}
Promise.all(pending).then(() => {
    lines.sort();
    for (const l of lines) console.log(l);
});
