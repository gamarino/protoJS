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
same("Object.assign reads strings, then symbols", log, ["p", "q", "a", "b"]);

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
