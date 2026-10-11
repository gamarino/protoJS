// Computed property keys that are data, not program vocabulary, live in a
// per-object dictionary of collectable strings (src/runtime/PropertyDictionary.h,
// docs/MEMORY_SAFETY_PROPOSALS.md section 2). This test runs every path that
// reads, writes, tests, deletes, describes or enumerates such keys and checks
// the results Node.js gives for the same script.
//
// Keys are built at run time ('dk' + ... ) and longer than six bytes, so they
// are heap strings that no part of the program names. Where a key ORDER is
// checked on an object that mixes such keys with identifier keys, the keys are
// sorted first: protoJS reports named keys in protoCore's attribute order (a
// documented deviation); dictionary keys keep insertion order, which is
// checked on objects holding only dictionary keys.
//
// Asserting test: exits 1 on any failure.
'use strict';

let failures = 0, checks = 0;
function eq(what, actual, expected) {
    checks++;
    const a = JSON.stringify(actual), e = JSON.stringify(expected);
    if (a !== e) { failures++; console.log('FAIL ' + what + ': got ' + a + ', expected ' + e); }
}
function throws(what, fn, ctor) {
    checks++;
    try { fn(); } catch (e) {
        if (!ctor || e instanceof ctor) return;
        failures++; console.log('FAIL ' + what + ': threw ' + e); return;
    }
    failures++; console.log('FAIL ' + what + ': did not throw');
}
const K = (i) => 'dk' + '-key-' + i;             // dynamic keys
const sorted = (a) => a.slice().sort();

// ---- 1. Element writes and reads -----------------------------------------------
{
    const o = {};
    for (let i = 0; i < 50; i++) o[K(i)] = i * 2;
    eq('read back', [o[K(0)], o[K(17)], o[K(49)]], [0, 34, 98]);
    eq('absent key', o[K(50)], undefined);
    o[K(17)] = 'changed';
    eq('overwrite', o[K(17)], 'changed');
    o[K(18)] = undefined;
    eq('undefined value is own', [o[K(18)], K(18) in o, o.hasOwnProperty(K(18))], [undefined, true, true]);
    eq('keys in insertion order', Object.keys(o).slice(0, 4), [K(0), K(1), K(2), K(3)]);
    eq('key count', Object.keys(o).length, 50);
    // The same key reached through another string with the same content.
    const again = ['dk-key-', '17'].join('');
    eq('content equality', o[again], 'changed');
    o.plain = 1;                                    // identifier key next to them
    eq('mixed keys', sorted(Object.keys(o)).slice(-1), ['plain']);
    eq('mixed count', Object.keys(o).length, 51);
}

// ---- 2. Identifier reads and writes of keys stored by content ---------------
{
    const o = {};
    o['late' + 'BoundName'] = 1;                   // the program names it below
    eq('identifier read', o.lateBoundName, 1);
    o.lateBoundName = 2;
    eq('identifier write', o['late' + 'BoundName'], 2);
    eq('one key', Object.keys(o), ['lateBoundName']);
    // A name that only code compiled later mentions.
    const p = JSON.parse('{"compiledLaterKey": 5, "otherLaterKey": 6}');
    const read = new Function('p', 'return p.compiledLaterKey + p.otherLaterKey;');
    eq('read by code compiled later', read(p), 11);
    const write = new Function('p', 'p.compiledLaterKey = 50; return Object.keys(p).length;');
    eq('write by code compiled later', [write(p), p.compiledLaterKey], [2, 50]);
}

// ---- 3. in / delete / hasOwnProperty / hasOwn / propertyIsEnumerable ---------
{
    const o = {};
    o[K(1)] = 1; o[K(2)] = 2; o[K(3)] = 3;
    eq('in', [K(1) in o, K(9) in o], [true, false]);
    eq('hasOwnProperty', [o.hasOwnProperty(K(2)), o.hasOwnProperty(K(9))], [true, false]);
    eq('Object.hasOwn', [Object.hasOwn(o, K(3)), Object.hasOwn(o, K(9))], [true, false]);
    eq('propertyIsEnumerable', o.propertyIsEnumerable(K(1)), true);
    eq('delete', [delete o[K(2)], K(2) in o, o[K(2)]], [true, false, undefined]);
    eq('delete absent', delete o[K(9)], true);
    o[K(2)] = 'back';
    eq('re-added key goes last', Object.keys(o), [K(1), K(3), K(2)]);
}

// ---- 4. Enumeration ------------------------------------------------------------
{
    const o = {};
    for (let i = 0; i < 5; i++) o[K(i)] = i;
    const forin = []; for (const k in o) forin.push(k);
    eq('for-in', forin, [K(0), K(1), K(2), K(3), K(4)]);
    eq('Object.values', Object.values(o), [0, 1, 2, 3, 4]);
    eq('Object.entries', Object.entries(o)[2], [K(2), 2]);
    eq('getOwnPropertyNames', Object.getOwnPropertyNames(o).length, 5);
    eq('Reflect.ownKeys', Reflect.ownKeys(o), [K(0), K(1), K(2), K(3), K(4)]);
    eq('JSON.stringify', JSON.stringify(o),
       '{"dk-key-0":0,"dk-key-1":1,"dk-key-2":2,"dk-key-3":3,"dk-key-4":4}');
    eq('Object.assign', Object.keys(Object.assign({}, o)), Object.keys(o));
    eq('spread', Object.values({ ...o }), [0, 1, 2, 3, 4]);
    const { [K(0)]: first, ...rest } = o;
    eq('rest', [first, Object.keys(rest)], [0, [K(1), K(2), K(3), K(4)]]);
    eq('Object.fromEntries', Object.fromEntries(Object.entries(o))[K(3)], 3);
    // (Spellings that also appear as literals in the program text are program
    // vocabulary and stay named attributes, so the group names are built.)
    const G = (s) => 'group' + '-of-' + s;
    const groups = Object.groupBy([1, 2, 3, 4], (n) => G(n % 2 ? 'odd' : 'even'));
    eq('Object.groupBy', [Object.keys(groups), groups[G('even')]], [[G('odd'), G('even')], [2, 4]]);
    // Integer keys of seven or more digits come first, ascending.
    const n = {};
    n[K(1)] = 'a'; n[2345678] = 'b'; n['1234567'] = 'c'; n[K(0)] = 'd';
    eq('index keys first', Object.keys(n), ['1234567', '2345678', K(1), K(0)]);
    eq('index read by number', [n[1234567], n['2345678']], ['c', 'b']);
}

// ---- 5. Descriptors -----------------------------------------------------------
{
    const o = {};
    o[K(1)] = 10;
    eq('getOwnPropertyDescriptor', Object.getOwnPropertyDescriptor(o, K(1)),
       { value: 10, writable: true, enumerable: true, configurable: true });
    eq('descriptor of absent', Object.getOwnPropertyDescriptor(o, K(2)), undefined);
    Object.defineProperty(o, K(2), { value: 20, enumerable: false });
    eq('defineProperty non-enumerable', [o[K(2)], Object.keys(o), Object.getOwnPropertyNames(o).length],
       [20, [K(1)], 2]);
}
{
    const o = {};
    o[K(3)] = 1;
    Object.defineProperty(o, K(3), { writable: false });
    throws('write to read-only key throws in strict code', () => { o[K(3)] = 2; }, TypeError);
    eq('read-only value kept', o[K(3)], 1);
    let backing = 0;
    Object.defineProperty(o, K(4), { get() { return backing + 100; }, set(v) { backing = v; },
                                     enumerable: true, configurable: true });
    o[K(4)] = 5;
    eq('accessor on computed key', [o[K(4)], backing], [105, 5]);
    const d = Object.getOwnPropertyDescriptors(o);
    eq('getOwnPropertyDescriptors', [d[K(3)].writable, typeof d[K(4)].get], [false, 'function']);
    const lit = { get [K(5)]() { return 'g'; }, [K(6)]: 'v' };
    eq('literal computed getter', [lit[K(5)], lit[K(6)], Object.keys(lit)], ['g', 'v', [K(5), K(6)]]);
    const viaDefineGetter = {};
    viaDefineGetter.__defineGetter__(K(7), () => 7);
    eq('__defineGetter__', viaDefineGetter[K(7)], 7);
}

// ---- 6. Integrity levels --------------------------------------------------------
{
    const o = {}; o[K(1)] = 1; o[K(2)] = 2;
    Object.freeze(o);
    eq('isFrozen', Object.isFrozen(o), true);
    throws('write to frozen throws in strict code', () => { o[K(1)] = 9; }, TypeError);
    throws('add to frozen throws in strict code', () => { o[K(3)] = 9; }, TypeError);
    throws('delete from frozen throws in strict code', () => { delete o[K(1)]; }, TypeError);
    eq('frozen values kept', [o[K(1)], K(3) in o, Object.keys(o).length], [1, false, 2]);
    eq('frozen descriptor', Object.getOwnPropertyDescriptor(o, K(2)),
       { value: 2, writable: false, enumerable: true, configurable: false });

    const s = {}; s[K(1)] = 1;
    Object.seal(s);
    s[K(1)] = 'updated';
    eq('sealed allows updates', s[K(1)], 'updated');
    throws('add to sealed throws', () => { s[K(2)] = 1; }, TypeError);
    throws('delete from sealed throws', () => { delete s[K(1)]; }, TypeError);
    eq('isSealed / isFrozen', [Object.isSealed(s), Object.isFrozen(s)], [true, false]);

    const p = {}; p[K(1)] = 1;
    Object.preventExtensions(p);
    p[K(1)] = 2;
    eq('non-extensible allows updates', p[K(1)], 2);
    throws('add to non-extensible throws', () => { p[K(5)] = 1; }, TypeError);
    eq('delete from non-extensible', [delete p[K(1)], K(1) in p], [true, false]);
}
(function sloppyIntegrity() {
    const f = new Function('o', 'k', 'o[k] = 99; return o[k];');
    const o = {}; o[K(1)] = 1; Object.freeze(o);
    eq('sloppy write to frozen is ignored', f(o, K(1)), 1);
})();

// ---- 7. Prototype chains ---------------------------------------------------------
{
    const base = {}; base[K(1)] = 'inherited'; base[K(2)] = 'base';
    const child = Object.create(base);
    child[K(2)] = 'own';
    eq('inherited dictionary key', child[K(1)], 'inherited');
    eq('own shadows inherited', child[K(2)], 'own');
    eq('in sees inherited', [K(1) in child, child.hasOwnProperty(K(1))], [true, false]);
    const seen = []; for (const k in child) seen.push(k);
    eq('for-in includes inherited', sorted(seen), [K(1), K(2)]);
    eq('Object.keys own only', Object.keys(child), [K(2)]);
    // A computed key that names an inherited built-in shadows it.
    const o = {};
    o['to' + 'String'] = () => 'mine';
    eq('shadow Object.prototype.toString', [String(o), o.toString()], ['mine', 'mine']);
    o['hasOwn' + 'Property'] = 42;
    eq('shadow hasOwnProperty', o.hasOwnProperty, 42);
    const q = {};
    q['__pro' + 'to__'] = base;
    eq('__proto__ computed write sets the prototype', [Object.getPrototypeOf(q) === base, q[K(1)]],
       [true, 'inherited']);
    class Point { constructor() { this['coord-' + 'xyz'] = 3; } get magic() { return 7; } }
    const pt = new Point();
    eq('class instance', [pt['coord-xyz'], pt.magic, Object.keys(pt)], [3, 7, ['coord-xyz']]);
}

// ---- 8. Proxy ------------------------------------------------------------------------
{
    const target = {}; target[K(1)] = 1;
    const plain = new Proxy(target, {});
    plain[K(2)] = 2;
    eq('proxy without traps', [plain[K(1)], target[K(2)], K(2) in plain, Object.keys(plain)],
       [1, 2, true, [K(1), K(2)]]);
    eq('proxy delete', [delete plain[K(1)], K(1) in target], [true, false]);
    eq('proxy descriptor', Object.getOwnPropertyDescriptor(plain, K(2)).value, 2);
    const log = [];
    const traced = new Proxy(target, {
        get(t, k, r) { log.push('get ' + String(k)); return Reflect.get(t, k, r); },
        set(t, k, v, r) { log.push('set ' + k); return Reflect.set(t, k, v, r); },
        has(t, k) { log.push('has ' + k); return Reflect.has(t, k); },
        deleteProperty(t, k) { log.push('delete ' + k); return Reflect.deleteProperty(t, k); },
        ownKeys(t) { log.push('ownKeys'); return Reflect.ownKeys(t); },
    });
    traced[K(3)] = 3;
    const got = traced[K(3)];
    const has = K(3) in traced;
    delete traced[K(3)];
    const keys = Object.keys(traced);
    eq('proxy traps', [got, has, keys, log],
       [3, true, [K(2)], ['set ' + K(3), 'get ' + K(3), 'has ' + K(3), 'delete ' + K(3), 'ownKeys']]);
}

// ---- 9. Reflect ------------------------------------------------------------------------
{
    const o = {};
    eq('Reflect.set', Reflect.set(o, K(1), 1), true);
    eq('Reflect.get', Reflect.get(o, K(1)), 1);
    eq('Reflect.has', [Reflect.has(o, K(1)), Reflect.has(o, K(2))], [true, false]);
    eq('Reflect.defineProperty', Reflect.defineProperty(o, K(2), { value: 2, enumerable: true }), true);
    eq('Reflect.getOwnPropertyDescriptor', Reflect.getOwnPropertyDescriptor(o, K(1)).value, 1);
    eq('Reflect.deleteProperty', [Reflect.deleteProperty(o, K(1)), K(1) in o], [true, false]);
    eq('Reflect.ownKeys', Reflect.ownKeys(o), [K(2)]);
    const recv = {};
    Reflect.set(o, K(3), 3, recv);
    eq('Reflect.set with receiver', [recv[K(3)], K(3) in o], [3, false]);
}

// ---- 10. JSON --------------------------------------------------------------------------
{
    const text = '{"id-0001-aaaa":{"name-field-x":"Ann","list-field":[1,"two",{"deep-key-1":true}]},' +
                 '"short-key":1,"id-0002-bbbb":null}';
    const p = JSON.parse(text);
    eq('parse values', [p['id-0001-aaaa']['name-field-x'], p['id-0001-aaaa']['list-field'][2]['deep-key-1'],
                        p['short-key'], p['id-0002-bbbb']], ['Ann', true, 1, null]);
    eq('parse order', Object.keys(p), ['id-0001-aaaa', 'short-key', 'id-0002-bbbb']);
    eq('parse round trip', JSON.stringify(p), text);
    const mixed = JSON.parse('{"zz-data-key":1,"short":2,"name":3,"aa-data-key":4}');
    eq('mixed parse keys', sorted(Object.keys(mixed)), ['aa-data-key', 'name', 'short', 'zz-data-key']);
    eq('mixed parse values', [mixed.short, mixed.name, mixed['zz-data-key']], [2, 3, 1]);
    eq('parsed array is an Array', [Array.isArray(p['id-0001-aaaa']['list-field']),
                                    p['id-0001-aaaa']['list-field'].map(x => typeof x)],
       [true, ['number', 'string', 'object']]);
    const revived = JSON.parse('{"keep-this-key":1,"drop-this-key":2}',
                               (k, v) => (k === 'drop-this-key' ? undefined : v));
    eq('reviver', Object.keys(revived), ['keep-this-key']);
    eq('parse prototype', Object.getPrototypeOf(p) === Object.prototype, true);
    const E = (i) => 'entry' + '-' + i;
    const big = {}; for (let i = 0; i < 2000; i++) big[E(i)] = i;
    const back = JSON.parse(JSON.stringify(big));
    eq('2000 keys round trip', [Object.keys(back).length, back[E(1999)], Object.keys(back)[1500]],
       [2000, 1999, E(1500)]);
}

// ---- 11. Arrays, functions and symbols keep their own paths ------------------
{
    const a = [1, 2];
    a['label-' + 'text'] = 'x';
    eq('array string key', [a['label-text'], a.length, Object.keys(a)], ['x', 2, ['0', '1', 'label-text']]);
    function f() {}
    f['meta-' + 'data'] = 1;
    eq('function string key', f['meta-data'], 1);
    const s = Symbol('sym-desc');
    const o = {}; o[s] = 1; o[K(1)] = 2;
    eq('symbol and dictionary keys', [o[s], Object.keys(o), Object.getOwnPropertySymbols(o).length],
       [1, [K(1)], 1]);
}

// ---- 12. Writes through setters and read-only inherited properties ----------
{
    const proto = {};
    let seen = null;
    Object.defineProperty(proto, 'setter-' + 'key', { set(v) { seen = v; }, configurable: true });
    Object.defineProperty(proto, 'readonly-' + 'key', { value: 1, writable: false });
    const o = Object.create(proto);
    o['setter-key'] = 5;
    eq('inherited setter runs', [seen, Object.keys(o)], [5, []]);
    throws('inherited read-only rejects in strict code', () => { o['readonly-key'] = 2; }, TypeError);
}

if (failures) {
    console.log('basic/dictionary_keys: ' + failures + ' of ' + checks + ' checks failed');
    process.exit(1);
}
console.log('basic/dictionary_keys: all ' + checks + ' checks passed');
