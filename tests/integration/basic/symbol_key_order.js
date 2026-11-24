// Symbol-keyed properties are reported in a fixed order on every platform.
//
// A symbol-keyed property is stored under an interned name "@@sym#<n>", and
// protoCore walks an object's attributes in key-address order -- the order in
// which the allocator happened to place those names. That order was stable on
// Linux and different on macOS and Windows, where Test262's own order tests
// failed (built-ins/Object/getOwnPropertySymbols/order-after-define-property,
// Reflect/ownKeys/order-after-define-property, Object/assign/
// strings-and-symbol-order, Object/{freeze,seal}/proxy-no-ownkeys-returned-
// keys-order). Every report of symbol keys now orders them by the symbols'
// creation, after the string keys, with array indices first.
//
// The relative order of named string keys is NOT asserted: they enumerate in
// protoCore's attribute order, a documented deviation from insertion order
// (docs/API_REFERENCE.md, "Property enumeration order", decided 2026-10-02),
// and that order changed with protoCore 3.6.1. What is asserted is what
// protoJS itself guarantees: integer keys first in ascending order, string
// keys before symbol keys, symbol keys by creation (here created in the
// order they are inserted).
//
// Asserting test: exits 1 on the first wrong order.

var failures = [];
function same(name, got, expected) {
    var ok = got.length === expected.length;
    for (var i = 0; ok && i < got.length; i++) ok = got[i] === expected[i];
    if (!ok) failures.push(name + ": got [" + got.map(String).join(", ") +
                           "], expected [" + expected.map(String).join(", ") + "]");
}

// Many symbols, so an order that merely happens to match is unlikely.
var syms = [];
for (var i = 0; i < 40; i++) syms.push(Symbol("s" + i));
var o = {};
for (var i = 0; i < syms.length; i++) o[syms[i]] = i;
same("getOwnPropertySymbols", Object.getOwnPropertySymbols(o), syms);
same("Reflect.ownKeys (symbols only)", Reflect.ownKeys(o), syms);

var a = Symbol("a"), b = Symbol("b");
var mixed = {};
mixed[a] = 1;
mixed.x = 2;
mixed[b] = 3;
mixed[0] = 4;
same("Reflect.ownKeys (index, string, symbols)", Reflect.ownKeys(mixed), ["0", "x", a, b]);

Object.defineProperty(mixed, a, { get: function () { return 1; } });
same("after redefining a", Object.getOwnPropertySymbols(mixed), [a, b]);

var log = [];
var source = {};
Object.defineProperty(source, a, { get: function () { log.push("a"); }, enumerable: true });
Object.defineProperty(source, "p", { get: function () { log.push("p"); }, enumerable: true });
Object.defineProperty(source, b, { get: function () { log.push("b"); }, enumerable: true });
Object.defineProperty(source, "q", { get: function () { log.push("q"); }, enumerable: true });
Object.assign({}, source);
// The two strings in either order (protoCore's), then the symbols by creation.
same("Object.assign reads strings, then symbols",
     [log.slice(0, 2).sort().join(","), log[2], log[3], log.length],
     ["p,q", "a", "b", 4]);

// Index keys ascending, then the string keys (any order), then the symbols.
var c = Symbol("c");
var kinds = {};
// The symbols are inserted in the order they were created, so creation order
// (protoJS) and insertion order (ECMA-262) agree.
kinds[a] = 1; kinds.zeta = 2; kinds[10] = 3; kinds[b] = 4; kinds.alpha = 5;
kinds[2] = 6; kinds[c] = 7; kinds.mid = 8;
var ks = Reflect.ownKeys(kinds);
same("Reflect.ownKeys: indices ascending", ks.slice(0, 2), ["2", "10"]);
same("Reflect.ownKeys: string keys (as a set)", ks.slice(2, 5).sort(), ["alpha", "mid", "zeta"]);
same("Reflect.ownKeys: symbols by creation", ks.slice(5), [a, b, c]);
same("Object.keys: indices, then string keys",
     [Object.keys(kinds).slice(0, 2).join(","), Object.keys(kinds).slice(2).sort().join(",")],
     ["2,10", "alpha,mid,zeta"]);

// Every other enumeration puts the indices first as well.
function indicesThenStrings(name, list) {
    same(name + ": indices ascending", list.slice(0, 2), ["2", "10"]);
    same(name + ": string keys (as a set)", list.slice(2).sort(), ["alpha", "mid", "zeta"]);
}
var forIn = [];
for (var fk in kinds) forIn.push(fk);
indicesThenStrings("for-in", forIn);
indicesThenStrings("Object.getOwnPropertyNames", Object.getOwnPropertyNames(kinds));
indicesThenStrings("Object.entries", Object.entries(kinds).map(function (e) { return e[0]; }));
// The values are numbers, so every quoted string in the text is a key.
indicesThenStrings("JSON.stringify", JSON.stringify(kinds).match(/"[^"]*"/g).map(function (q) { return JSON.parse(q); }));
var revived = [];
JSON.parse(JSON.stringify(kinds), function (k, v) { if (k !== "") revived.push(k); return v; });
indicesThenStrings("JSON.parse reviver", revived);

// Deleting and re-adding keys keeps the classes in order. A re-added symbol
// keeps its creation rank in protoJS (ECMA-262 would move it last), so only
// string and index keys are deleted and re-added here.
delete kinds[2]; delete kinds.zeta;
kinds[2] = 9; kinds.zeta = 10; kinds[0] = 12;
ks = Reflect.ownKeys(kinds);
same("after delete and re-add: indices ascending", ks.slice(0, 3), ["0", "2", "10"]);
same("after delete and re-add: string keys (as a set)", ks.slice(3, 6).sort(), ["alpha", "mid", "zeta"]);
same("after delete and re-add: symbols by creation", ks.slice(6), [a, b, c]);

var target = {};
target[a] = 1;
target.foo = 2;
target[0] = 3;
var seen = [];
var proxy = new Proxy(target, {
    getOwnPropertyDescriptor: function (t, k) { seen.push(k); return Reflect.getOwnPropertyDescriptor(t, k); }
});
Object.freeze(proxy);
same("Object.freeze through a proxy without ownKeys", seen, ["0", "foo", a]);

if (failures.length) {
    for (var i = 0; i < failures.length; i++) console.log("FAIL: " + failures[i]);
    process.exit(1);
}
console.log("basic/symbol_key_order: all orders as specified");
