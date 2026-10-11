// A getter or setter defined with a computed key gets the function name
// "get <key>" / "set <key>", and a symbol key contributes "[<description>]"
// (ECMA-262 SetFunctionName, §10.2.9, called from MethodDefinition evaluation
// with the prefix "get" or "set"). A computed method gets the bare key.
// protoJS named accessors only when the key was a literal name, so
// `({ get [k]() {} })` left the getter with an empty name. Expected values
// were checked against Node.js.

let failures = 0;
function check(actual, expected, label) {
    const a = JSON.stringify(actual), e = JSON.stringify(expected);
    if (a !== e) { failures++; console.log("FAIL " + label + ": expected " + e + ", got " + a); }
}
function names(obj, key) {
    const d = Object.getOwnPropertyDescriptor(obj, key);
    return [d.get ? d.get.name : null, d.set ? d.set.name : null];
}

const k = "fo" + "o";
const sym = Symbol("desc");
const anon = Symbol();

// Object literals.
const lit = {
    get [k]() { return 1; }, set [k](v) {},
    get [sym]() { return 2; }, set [sym](v) {},
    get [anon]() { return 3; },
    get [1 + 1]() { return 4; },
    [k + "M"]() {},
};
check(names(lit, "foo"), ["get foo", "set foo"], "literal string key");
check(names(lit, sym), ["get [desc]", "set [desc]"], "literal symbol key");
check(names(lit, anon), ["get ", null], "literal symbol key without description");
check(names(lit, "2"), ["get 2", null], "literal numeric key");
check(lit.fooM.name, "fooM", "literal computed method");

const symMethod = { [sym]() {} };
check(symMethod[sym].name, "[desc]", "literal computed symbol method");

// Literal (non-computed) accessors keep their names.
const plain = { get bar() { return 0; }, set bar(v) {} };
check(names(plain, "bar"), ["get bar", "set bar"], "literal non-computed accessors");

// Classes: prototype and static accessors.
class C {
    get [k]() { return 1; }
    set [k](v) {}
    static get [sym]() { return 2; }
    static set [sym](v) {}
    [k + "M"]() {}
    static [sym.toString()]() {}
}
check(names(C.prototype, "foo"), ["get foo", "set foo"], "class prototype computed accessors");
check(names(C, sym), ["get [desc]", "set [desc]"], "class static computed symbol accessors");
check(C.prototype.fooM.name, "fooM", "class computed method");
check(C["Symbol(desc)"].name, "Symbol(desc)", "class static computed method");

// The name property is { writable: false, enumerable: false, configurable: true }.
const nd = Object.getOwnPropertyDescriptor(Object.getOwnPropertyDescriptor(lit, "foo").get, "name");
check([nd.writable, nd.enumerable, nd.configurable], [false, false, true], "name descriptor");

if (failures) { console.log(failures + " failure(s)"); process.exit(1); }
console.log("computed_accessor_names: OK");
