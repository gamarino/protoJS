// Enumerability of methods and accessors (ECMA-262 §13.2.5.5 and §15.4):
// in an object literal every method, getter and setter -- computed key or
// not -- is enumerable; in a class body every method, getter and setter,
// static or not, is non-enumerable. Accessors are also configurable and have
// no `value`/`writable`. Expected values were checked against Node.js.

let failures = 0;
function check(actual, expected, label) {
    const a = JSON.stringify(actual), e = JSON.stringify(expected);
    if (a !== e) { failures++; console.log("FAIL " + label + ": expected " + e + ", got " + a); }
}
function flags(obj, key) {
    const d = Object.getOwnPropertyDescriptor(obj, key);
    return [d.enumerable, d.configurable, "writable" in d ? d.writable : "-"];
}

const k = "c" + "omp";
const sym = Symbol("s");

const lit = {
    get g() { return 1; },
    set s(v) {},
    get [k + "G"]() { return 2; },
    set [k + "S"](v) {},
    get [sym]() { return 3; },
    m() {},
    [k + "M"]() {},
    *gen() {},
    async am() {},
};
check(flags(lit, "g"), [true, true, "-"], "literal getter");
check(flags(lit, "s"), [true, true, "-"], "literal setter");
check(flags(lit, "compG"), [true, true, "-"], "literal computed getter");
check(flags(lit, "compS"), [true, true, "-"], "literal computed setter");
check(flags(lit, sym), [true, true, "-"], "literal computed symbol getter");
check(flags(lit, "m"), [true, true, true], "literal method");
check(flags(lit, "compM"), [true, true, true], "literal computed method");
check(flags(lit, "gen"), [true, true, true], "literal generator method");
check(flags(lit, "am"), [true, true, true], "literal async method");
// Sorted: protoJS's key order is not insertion order, a documented deviation
// (docs/API_REFERENCE.md); this test is about which keys are enumerable.
const enumerableKeys = ["am", "compG", "compM", "compS", "g", "gen", "m", "s"];
check(Object.keys(lit).sort(), enumerableKeys, "Object.keys of a literal");
const seen = [];
for (const key in lit) seen.push(key);
check(seen.sort(), enumerableKeys, "for-in over a literal");

class C {
    get g() { return 1; }
    set s(v) {}
    get [k + "G"]() { return 2; }
    set [k + "S"](v) {}
    m() {}
    [k + "M"]() {}
    static get sg() { return 3; }
    static [k + "SM"]() {}
    static get [k + "SG"]() { return 4; }
}
for (const key of ["g", "s", "compG", "compS"])
    check(flags(C.prototype, key), [false, true, "-"], "class accessor " + key);
for (const key of ["m", "compM"])
    check(flags(C.prototype, key), [false, true, true], "class method " + key);
check(flags(C, "sg"), [false, true, "-"], "class static getter");
check(flags(C, "compSG"), [false, true, "-"], "class static computed getter");
check(flags(C, "compSM"), [false, true, true], "class static computed method");
check(Object.keys(C.prototype), [], "Object.keys of a class prototype");
check(Object.keys(C), [], "Object.keys of a class");

if (failures) { console.log(failures + " failure(s)"); process.exit(1); }
console.log("method_enumerability: OK");
