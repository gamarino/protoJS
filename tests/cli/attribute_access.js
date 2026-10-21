// Named property reads on the one-lookup path of OP_get_field / OP_get_field2
// against Node: own data, inherited data and methods, accessors (own, class,
// defineProperty, built-in, nearer than inherited data), proxies as receiver
// and as ancestor, setPrototypeOf, symbols, lazy prototypes, deep chains.
const out = [];
const p = (...a) => out.push(a.map(String).join(' '));
class Box { constructor() { this.x = 1; this.u = undefined; this.n = null; } m() { return 'm'; } get g() { return 'getter'; } }
const b = new Box();
p('own', b.x, b.u, b.n, b.m(), b.g, typeof b.m);
class Sub extends Box { m() { return 'sub+' + super.m(); } }
const s = new Sub(); p('inherit', s.x, s.m(), s.g);
const o = { a: 1, get acc() { return 'own accessor'; } };
p('literal', o.a, o.acc);
Object.defineProperty(o, 'dp', { get() { return 'defined'; }, configurable: true });
p('defineProperty', o.dp);
const base = { v: 'base' }; const mid = Object.create(base);
Object.defineProperty(mid, 'v', { get() { return 'mid getter'; } });
const leaf = Object.create(mid); p('nearer getter', leaf.v);
Object.prototype.size = 'polluted';
const m = new Map([[1, 2]]); p('map size', m.size);
class MyMap extends Map {} p('submap size', new MyMap([[1, 1], [2, 2]]).size);
delete Object.prototype.size;
const prx = new Proxy({ t: 'target' }, { get(t, k) { return k === 't' ? 'trapped' : Reflect.get(t, k); } });
p('proxy', prx.t, prx.toString === Object.prototype.toString);
const viaProxy = Object.create(new Proxy({}, { get(t, k) { return 'from trap ' + String(k); } }));
p('proxy ancestor', viaProxy.anything);
const sp1 = { w: 'one' }, sp2 = { w: 'two' }; const obj = Object.create(sp1);
Object.setPrototypeOf(obj, sp2); p('setPrototypeOf', obj.w);
const sym = Symbol('k'); const so = { [sym]: 'symval' }; p('symbol', so[sym], sym.description);
function F() {} p('lazy prototype', typeof F.prototype, F.prototype.constructor === F);
p('string', 'abc'.length, 'abc'.toUpperCase());
p('array', [1, 2, 3].length, [1, 2].map(x => x * 2).join(','));
p('__proto__', Object.getPrototypeOf(b) === Box.prototype, b.__proto__ === Box.prototype);
const fr = Object.freeze({ f: 7 }); p('frozen', fr.f);
class Static { static k = 'static'; static sm() { return 'sm'; } } p('static', Static.k, Static.sm());
const cnt = { c: 0, inc() { this.c++; return this.c; } }; cnt.inc(); cnt.inc(); p('counter', cnt.c);
const later = { y: 1 }; Object.defineProperty(later, 'y', { get() { return 'redefined'; } }); p('redefine data->accessor', later.y);
const deep = Object.create(Object.create(Object.create({ z: 'deep' }))); p('deep', deep.z);
const ta = new Uint8Array([5, 6]); p('typed', ta.length, ta[1], ta.byteLength);
p('regexp', /a/g.flags, /a/g.global);
try { null.x; } catch (e) { p('null read', e instanceof TypeError); }
console.log(out.join('\n'));
