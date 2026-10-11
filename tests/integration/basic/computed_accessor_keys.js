// A getter or setter with a computed key is installed under ToPropertyKey of
// the key, so `get [undefined]()` defines the accessor "undefined" and
// `set [null](v)` the accessor "null" (ECMA-262 §13.2.5.5, §7.1.19). protoJS
// stored such an accessor without marking the object as having own accessors,
// so an element read or write (`o[null]`, `o["null"]`, `o[key]`) skipped it and
// returned undefined; only a property-name read (`o.foo`) found it. Test262's
// cpn-class-*-accessors-computed-property-name-from-null and -from-yield-
// expression failed on this. Expected values were checked against Node.js.

let failures = 0;
function check(actual, expected, label) {
    const a = JSON.stringify(actual), e = JSON.stringify(expected);
    if (a !== e) { failures++; console.log("FAIL " + label + ": expected " + e + ", got " + a); }
}

// Object literals.
const lit = { get [undefined]() { return "u"; }, get [null]() { return "n"; } };
check([lit[undefined], lit["undefined"], lit.undefined], ["u", "u", "u"], "literal getter [undefined]");
check([lit[null], lit["null"], lit.null], ["n", "n", "n"], "literal getter [null]");

const box = { set [null](v) { this.seen = v; } };
box[null] = 1;
check(box.seen, 1, "literal setter [null] via o[null] = v");
box["null"] = 2;
check(box.seen, 2, "literal setter [null] via o['null'] = v");

const nul = null, und = undefined;
const fromVars = { get [nul]() { return "vn"; }, set [und](v) { this.u = v; } };
fromVars[und] = 3;
check([fromVars["null"], fromVars.u], ["vn", 3], "keys from variables");

// Any computed accessor key, not only undefined and null, is read by element access.
const k = "fo" + "o";
const s = { get [k]() { return "f"; }, get ["bar"]() { return "b"; } };
check([s[k], s["foo"], s.foo, s["bar"], s.bar], ["f", "f", "f", "b", "b"], "computed string keys");
const sym = Symbol("q");
const symObj = { get [sym]() { return "sym"; } };
check(symObj[sym], "sym", "computed symbol key");

// Classes: prototype and static accessors.
class C {
    get [null]() { return "pn"; }
    set [null](v) { this.v = v; }
    static get [null]() { return "sn"; }
    static get [undefined]() { return "su"; }
}
const c = new C();
c[null] = 7;
check([c[null], c["null"], c.v], ["pn", "pn", 7], "class prototype accessors [null]");
check([C[null], C["null"], C[undefined], C["undefined"]], ["sn", "sn", "su", "su"], "class static accessors");

// The key produced by a yield that received undefined (the Test262 shape).
function* g() {
    const D = class { get [yield 1]() { return 9; } static get [yield 2]() { return 8; } };
    return [new D()["undefined"], D[undefined]];
}
const it = g();
it.next(); it.next();
check(it.next().value, [9, 8], "yield-computed keys");

const desc = Object.getOwnPropertyDescriptor(C.prototype, "null");
check([typeof desc.get, typeof desc.set, "value" in desc], ["function", "function", false], "descriptor is an accessor");

if (failures) { console.log(failures + " failure(s)"); process.exit(1); }
console.log("computed_accessor_keys: OK");
