// A write that adds a property must not build accessor sidecar names when no
// accessor can apply.  Until 2026-10 every such write (resolvePutFieldOOP)
// built "__set_<key>__" and "__get_<key>__" as heap strings and probed them
// at every level of the prototype chain: its gate, __has_accessor_props__
// read through the chain, is always true because Object.prototype carries
// it for its __proto__ accessor.  Run by noninterned_names.cmake, which
// fails when the process passes more than a small constant number of
// non-interned attribute names; this script adds 4 properties to each of
// 20,000 objects.  Results are checked (exit 1 on a wrong one).
// Sloppy mode on purpose: writes to getter-only accessors fail silently.
var N = 20000;
var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + got + ", expected " + expected);
}
function Point() {}
var sum = 0;
for (var i = 0; i < N; i++) {
    var o = {};
    o.x = i;
    o.label = "p";
    o.someLongerName = i * 2;
    var p = new Point();
    p.coordinate = i;
    sum += o.x + o.someLongerName + p.coordinate;
}
check("sum", sum, 4 * (N * (N - 1)) / 2);
// Accessors still apply.
var seen = 0;
var proto = { set target(v) { seen = v; }, get readOnly() { return 1; } };
var child = Object.create(proto);
child.target = 5;
check("inherited setter", seen, 5);
check("no own data from setter", Object.prototype.hasOwnProperty.call(child, "target"), false);
child.readOnly = 9;
check("getter-only inherited", child.readOnly, 1);
var q = {};
var newProto = { marker: 7 };
q.__proto__ = newProto;
check("__proto__ setter", Object.getPrototypeOf(q), newProto);
var m = new Map([[1, 2]]);
m.size = 10;
check("Map size getter-only", m.size, 1);
function Ctor() {}
Object.defineProperty(Ctor.prototype, "v", { set: function (x) { seen = x * 2; }, configurable: true });
new Ctor().v = 21;
check("defineProperty setter on a prototype", seen, 42);
if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("noninterned_property_writes: " + 4 * N + " property additions, all results correct");
