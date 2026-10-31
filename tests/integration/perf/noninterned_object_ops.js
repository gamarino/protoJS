// Object operations must not build sidecar names (__get_K__, __set_K__,
// __pd_K__) as heap strings when no accessor or descriptor can apply, and
// fixed names must reach protoCore interned.  Run by noninterned_names.cmake,
// which fails when the process passes more than a small constant number of
// non-interned attribute names.  Each operation below runs N times; before
// the fix each one canonicalised one or more heap-string names per call.
// Results are checked (exit 1 on a wrong one).
//
// Not covered, because they remain one canonicalisation per operation until
// protoCore offers a lookup that does not intern: a data key computed at run
// time (a heap string, 7 bytes or more) that no dictionary holds, and the
// sidecar probe of a key of 6 bytes or fewer (an inline string) on an object
// that does hold descriptors or accessors.  The keys below avoid both.
"use strict";
var N = 5000;
var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + got + ", expected " + expected);
}

// Array.from over an iterable: the @@iterator getter probe and the
// done/value getter probes of every step.
var set = new Set();
for (var i = 0; i < N; i++) set.add(i);
var fromSet = Array.from(set);
check("Array.from(Set) length", fromSet.length, N);
check("Array.from(Set) last", fromSet[N - 1], N - 1);

// Reads of a missing property and `in` on a missing key (inherited
// accessor probes).
var plain = { present: 1 };
var misses = 0, ins = 0;
for (var i = 0; i < N; i++) {
    if (plain.absentProperty === undefined) misses++;
    if ("anotherAbsentKey" in plain) ins++;
}
check("missing reads", misses, N);
check("missing in", ins, 0);

// Object.keys / Object.values on objects with a non-writable property (the
// per-key __pd_K__ probe) and an own getter (the __get_K__ probe).
var described = { alphaKey: 1, betaGamma: 2 };
Object.defineProperty(described, "frozenField", { value: 3, writable: false, enumerable: true });
Object.defineProperty(described, "computed", { get: function () { return 4; }, enumerable: true });
var keySum = 0;
for (var i = 0; i < N; i++) {
    // Sum, not positions: the order of named keys is not specified.
    var vals = Object.values(described);
    keySum += vals.length + vals[0] + vals[1] + vals[2] + vals[3];
}
check("Object.values", keySum, N * 14);

// Writes to a non-extensible object (the __set_K__ probe) fail with a
// TypeError in strict mode.
var sealedObj = Object.preventExtensions({ existing: 1 });
var rejected = 0;
for (var i = 0; i < N; i++) {
    try { sealedObj.newProperty = i; } catch (e) { if (e instanceof TypeError) rejected++; }
}
check("non-extensible writes rejected", rejected, N);

// Object.freeze of an array and of an object: one interned descriptor name
// per key, read and written as a symbol.
var arr = [];
for (var i = 0; i < N; i++) arr.push(i);
Object.freeze(arr);
check("frozen element", Object.getOwnPropertyDescriptor(arr, "17").writable, false);
var wide = {};
for (var i = 0; i < 100; i++) wide["field" + i] = i;
Object.seal(wide);
check("sealed field", Object.getOwnPropertyDescriptor(wide, "field42").configurable, false);

// Object.fromEntries and new Map over an iterable of pairs: the done/value
// getter probes and the index getter probes of each pair.
var pairs = [];
for (var i = 0; i < N; i++) pairs.push(["k" + i, i]);
var m = new Map(pairs);
check("Map from pairs", m.get("k17"), 17);
var fe = Object.fromEntries(m);
check("fromEntries", fe.k42, 42);

// Accessors still apply where they exist.
var inherited = Object.create({ get viaProto() { return "g"; } });
check("inherited getter", inherited.viaProto, "g");
check("inherited getter in", "viaProto" in inherited, true);
var pairWithGetter = [];
Object.defineProperty(pairWithGetter, "0", { get: function () { return "k"; } });
pairWithGetter[1] = "v";
check("Map pair getter", new Map([pairWithGetter]).get("k"), "v");
function argsLength() { return arguments.length; }
var argTotal = 0;
for (var i = 0; i < N; i++) argTotal += argsLength(1, 2);
check("arguments", argTotal, 2 * N);

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("noninterned_object_ops: all results correct");
