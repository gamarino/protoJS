// `super.x` reads and writes go through the home object's prototype with the
// caller's `this` as receiver (ECMA-262 §13.3.7, GetValue / PutValue of a
// super reference: superBase.[[Get]](key, this) and
// superBase.[[Set]](key, value, this)). protoJS read the raw attribute, so an
// inherited getter returned undefined instead of running, and wrote the value
// straight onto `this`, so an inherited setter never ran.
// Expected values were checked against Node.js.

"use strict";
let failures = 0;
function check(actual, expected, label) {
    const a = JSON.stringify(actual), e = JSON.stringify(expected);
    if (a !== e) { failures++; console.log("FAIL " + label + ": expected " + e + ", got " + a); }
}

class Base {
    get size() { return this.n * 2; }
    set size(v) { this.n = v / 2; }
    get readOnly() { return "ro"; }
}
class Derived extends Base {
    constructor() { super(); this.n = 3; }
    get size() { return super.size + 1; }
    set size(v) { super.size = v + 2; }
    plain() { return super.size; }
    writeData(v) { super.extra = v; }
    writeReadOnly() { super.readOnly = 1; }
}
const d = new Derived();
check(d.plain(), 6, "super getter runs with the caller's this");
check(d.size, 7, "super getter from an overriding getter");
d.size = 10;
check(d.n, 6, "super setter runs with the caller's this");
d.writeData(5);
check([d.extra, Object.prototype.hasOwnProperty.call(d, "extra"),
       Object.prototype.hasOwnProperty.call(Base.prototype, "extra")],
      [5, true, false], "a data write through super lands on this");
let threw = "no";
try { d.writeReadOnly(); } catch (e) { threw = e.constructor.name; }
check(threw, "TypeError", "strict write to a getter-only accessor through super");

// Object literals and symbol keys.
const sym = Symbol("k");
const proto = { get [sym]() { return "sym " + this.tag; }, get v() { return this.tag; } };
const obj = {
    __proto__: proto,
    tag: "obj",
    read() { return [super.v, super[sym]]; },
};
check(obj.read(), ["obj", "sym obj"], "literal: super getters with string and symbol keys");

// Static members.
class S { static get who() { return "S:" + this.name; } }
class T extends S { static who2() { return super.who; } }
check(T.who2(), "S:T", "static super getter receives the derived class");

if (failures) { console.log(failures + " failure(s)"); process.exit(1); }
console.log("super_property_accessors: OK");
