// Own properties shadow inherited ones, also when the own property is a key of
// the object's property dictionary (src/runtime/PropertyDictionary.h) and a
// prototype gains a NAMED property of the same spelling later.
//
// A run-time key goes to the dictionary only when no loaded code names it and
// no prototype of the object has it as a named attribute. Both can change
// afterwards: code loaded later (new Function, require) names the key,
// which makes it program vocabulary, and a prototype gains it as a named
// property (an assignment by name, a getter/setter defined by
// Object.defineProperty or a class, Object.setPrototypeOf). Before the fix a
// static read `o.k` then returned the prototype's value instead of the own
// dictionary entry (docs/MEMORY_SAFETY_PROPOSALS.md, the former "known limit").
//
// Every key below is built at run time and is named only by code compiled after
// the objects store it, so the objects hold it in their dictionaries. Each key
// spelling is used by one section only: once code names it, it is vocabulary
// and no later object would store it in a dictionary.
//
// Asserting test: exits 1 on any failure. The expected values are those Node.js
// gives for the same script.
'use strict';

let failures = 0, checks = 0;
function eq(what, actual, expected) {
    checks++;
    const a = JSON.stringify(actual), e = JSON.stringify(expected);
    if (a !== e) { failures++; console.log('FAIL ' + what + ': got ' + a + ', expected ' + e); }
}
const key = (tag) => ['shadow', 'Probe', tag].join('');     // e.g. shadowProbeA1
// Code compiled now: the key spelling becomes vocabulary from this call on.
const late = (params, body) => new Function(...params, body);

// Reads of key `k` by name, by literal and reflectively, compiled late.
function probe(k) {
    return late(['o'], `return [
        o.${k},
        o['${k}'],
        '${k}' in o,
        Object.prototype.hasOwnProperty.call(o, '${k}'),
        Object.hasOwn(o, '${k}'),
        (Object.getOwnPropertyDescriptor(o, '${k}') || {}).value,
        Reflect.get(o, '${k}'),
        Reflect.has(o, '${k}'),
        Object.keys(o).filter((x) => x === '${k}').length,
        Reflect.ownKeys(o).filter((x) => x === '${k}').length
    ];`);
}
const ownView = (v) => [v, v, true, true, true, v, v, true, 1, 1];

// ---- 1. A class prototype gains a named data property ------------------------
{
    const k = key('A1');
    class C {}
    const c = new C();
    c[k] = 'own';
    c.other = 1;
    late(['C'], `C.prototype.${k} = 'proto';`)(C);
    eq('class: reads', probe(k)(c), ownView('own'));
    eq('class: fresh instance inherits', late(['C'], `return new C().${k};`)(C), 'proto');
    // Key order is not checked: named keys precede dictionary keys (the
    // documented deviation, docs/API_REFERENCE.md).
    const json = JSON.parse(JSON.stringify(c));
    eq('class: JSON', [json[k], json.other, Object.keys(json).length], ['own', 1, 2]);
    eq('class: JSON round trip', JSON.parse(JSON.stringify(c))[k], 'own');
    late(['c'], `c.${k} = 'own2';`)(c);
    eq('class: named write updates the own entry',
       [c[k], C.prototype[k], Object.keys(c).length, Object.getOwnPropertyNames(c).length],
       ['own2', 'proto', 2, 2]);
    eq('class: delete re-exposes the prototype', late(['c'], `delete c.${k}; return [c.${k}, '${k}' in c, Object.hasOwn(c, '${k}')];`)(c),
       ['proto', true, false]);
}

// ---- 2. Object.prototype gains a named data property --------------------------
{
    const k = key('B2');
    const o = {};
    o[k] = 'own';
    const parsed = JSON.parse('{"' + k + '": "parsed"}');
    // (protoJS does not execute eval(): README, "eval() execution".)
    late([], `Object.prototype.${k} = 'proto';`)();
    eq('Object.prototype: reads', probe(k)(o), ownView('own'));
    eq('Object.prototype: parsed object', probe(k)(parsed), ownView('parsed'));
    eq('Object.prototype: plain object inherits', late([], `return ({}).${k};`)(), 'proto');
    eq('Object.prototype: spread and assign',
       [late(['o'], `return ({ ...o }).${k};`)(o), late(['o'], `return Object.assign({}, o).${k};`)(o)],
       ['own', 'own']);
    const seen = [];
    for (const x in o) if (x === k) seen.push(o[x]);
    eq('Object.prototype: for-in lists the key once', seen, ['own']);
    eq('Object.prototype: JSON round trip', JSON.parse(JSON.stringify(parsed))[k], 'parsed');
    delete Object.prototype[k];
}

// ---- 3. A getter and setter defined on the prototype later -------------------
{
    const k = key('C3');
    const proto = { log: [] };
    const o = Object.create(proto);
    o[k] = 'own';
    late(['p'], `Object.defineProperty(p, '${k}', {
        get() { return 'getter'; },
        set(v) { this.log.push(v); },
        configurable: true });`)(proto);
    eq('accessor: reads', probe(k)(o), ownView('own'));
    eq('accessor: computed read', o[k], 'own');
    late(['o'], `o.${k} = 'by name';`)(o);
    eq('accessor: named write updates the own entry, no setter call',
       [o[k], proto.log.length], ['by name', 0]);
    o[k] = 'by key';
    eq('accessor: computed write, no setter call', [o[k], proto.log.length], ['by key', 0]);
    eq('accessor: Reflect.set', [Reflect.set(o, k, 'reflect'), o[k], proto.log.length],
       [true, 'reflect', 0]);
    const fresh = Object.create(proto);
    late(['f'], `f.${k} = 'through setter';`)(fresh);
    eq('accessor: an object without the key uses the setter',
       [proto.log, late(['f'], `return f.${k};`)(fresh)], [['through setter'], 'getter']);
}

// ---- 4. A class with accessors becomes the prototype (setPrototypeOf) --------
{
    const k = key('D4');
    const o = {};
    o[k] = 'own';
    const G = late([], `return class { get ${k}() { return 'getter'; } set ${k}(v) { this.called = v; } };`)();
    Object.setPrototypeOf(o, G.prototype);
    eq('setPrototypeOf: reads', probe(k)(o), ownView('own'));
    late(['o'], `o.${k} = 'w';`)(o);
    eq('setPrototypeOf: write', [o[k], o.called], ['w', undefined]);
}

// ---- 5. Frozen objects ---------------------------------------------------------
{
    const k = key('E5');
    const proto = { calls: 0 };
    const o = Object.create(proto);
    o[k] = 'own';
    Object.freeze(o);
    late(['p'], `Object.defineProperty(p, '${k}', {
        get() { return 'getter'; }, set(v) { this.calls++; }, configurable: true });`)(proto);
    eq('frozen: reads', probe(k)(o), ownView('own'));
    let threw = false;
    try { late(['o'], `'use strict'; o.${k} = 'x';`)(o); } catch (e) { threw = e instanceof TypeError; }
    eq('frozen: strict named write throws, no setter call', [threw, o[k], proto.calls], [true, 'own', 0]);
    late(['o'], `o.${k} = 'y';`)(o);                 // sloppy: fails silently
    eq('frozen: sloppy named write ignored', [o[k], proto.calls], ['own', 0]);
    eq('frozen: descriptor', late(['o'], `const d = Object.getOwnPropertyDescriptor(o, '${k}');
        return [d.value, d.writable, d.enumerable, d.configurable];`)(o), ['own', false, true, false]);
    eq('frozen: isFrozen', Object.isFrozen(o), true);
}

// ---- 6. Proxies ------------------------------------------------------------------
{
    const k = key('F6');
    class P {}
    const target = new P();
    target[k] = 'own';
    const plain = new Proxy(target, {});
    const reflecting = new Proxy(target, { get: (t, p, r) => Reflect.get(t, p, r) });
    late(['P'], `P.prototype.${k} = 'proto';`)(P);
    eq('proxy: no traps', probe(k)(plain), ownView('own'));
    eq('proxy: Reflect.get trap', late(['p'], `return p.${k};`)(reflecting), 'own');
    late(['p'], `p.${k} = 'via proxy';`)(plain);
    eq('proxy: write reaches the own entry', [target[k], P.prototype[k]], ['via proxy', 'proto']);
}

// ---- 7. An inheriting object: an intermediate dictionary shadows a higher named key
{
    const k = key('G7');
    class Base {}
    const mid = new Base();
    mid[k] = 'mid';
    const child = Object.create(mid);
    late(['B'], `Object.defineProperty(B.prototype, '${k}', {
        get() { return 'top getter'; }, set(v) { this.topSet = v; }, configurable: true });`)(Base);
    eq('intermediate: reads', late(['c'], `return [c.${k}, c['${k}'], '${k}' in c, Object.hasOwn(c, '${k}'), Reflect.get(c, '${k}')];`)(child),
       ['mid', 'mid', true, false, 'mid']);
    eq('intermediate: computed read', child[k], 'mid');
    // OrdinarySet finds mid's writable data property first: the write creates
    // an own data property on child; it calls no setter and leaves mid alone.
    late(['c'], `c.${k} = 'child';`)(child);
    eq('intermediate: write', [child[k], mid[k], Object.hasOwn(child, k), child.topSet], ['child', 'mid', true, undefined]);
}

// ---- 8. A key named by a module loaded later (require) ------------------------
{
    // The module source names this spelling; the main script builds it.
    const k = ['shadow', 'Module', 'LateKey'].join('');
    class M {}
    const m = new M();
    m[k] = 'own';
    const parsed = JSON.parse('{"' + k + '": "parsed"}');
    const lateModule = require('./fixtures/dictionary_shadowing_module.js');
    lateModule.install(M);
    eq('module: reads', [lateModule.read(m), lateModule.read(parsed), lateModule.read(new M())],
       ['own', 'parsed', 'proto']);
    lateModule.write(m, 'written');
    eq('module: write', [m[k], M.prototype[k]], ['written', 'proto']);
}

// ---- 9. A named key that is not vocabulary (computed defineProperty) -------
{
    // Never named by any code: only computed and reflective reads reach it.
    const k = key('H9');
    const proto = {};
    const o = Object.create(proto);
    o[k] = 'own';
    const child = Object.create(o);
    Object.defineProperty(proto, k, { get() { return 'getter'; }, configurable: true });
    eq('non-vocabulary: reads', [o[k], k in o, Object.hasOwn(o, k), Reflect.get(o, k),
                                 Object.getOwnPropertyDescriptor(o, k).value],
       ['own', true, true, 'own', 'own']);
    eq('non-vocabulary: inheriting object', [child[k], Reflect.get(child, k)], ['own', 'own']);
    eq('non-vocabulary: JSON', JSON.parse(JSON.stringify(o))[k], 'own');
}

if (failures) {
    console.log('basic/dictionary_shadowing: ' + failures + ' of ' + checks + ' checks failed');
    process.exit(1);
}
console.log('basic/dictionary_shadowing: all ' + checks + ' checks passed');
