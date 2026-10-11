// A method stored under a symbol key is called through `o[sym](...)`.
// protoJS read `o[sym]` correctly, but the call form (QuickJS's
// get_array_el2 / get_array_el3, which keep the receiver for the call)
// converted the symbol with ToString to "Symbol(desc)" instead of using the
// symbol's own key, found nothing and threw "is not a function".
// Expected values were checked against Node.js.

let failures = 0;
function check(actual, expected, label) {
    const a = JSON.stringify(actual), e = JSON.stringify(expected);
    if (a !== e) { failures++; console.log("FAIL " + label + ": expected " + e + ", got " + a); }
}
function attempt(fn) {
    try { return fn(); } catch (e) { return "threw " + e.constructor.name; }
}

const sym = Symbol("s");
const anon = Symbol();

const assigned = {};
assigned[sym] = function () { return this === assigned ? "assigned" : "wrong this"; };
check(attempt(() => assigned[sym]()), "assigned", "method assigned through a symbol");

const field = { [sym]: function () { return "field"; } };
check(attempt(() => field[sym]()), "field", "computed field holding a function");

const method = { [sym](a, b) { return a + b; }, [anon]() { return "anon"; } };
check(attempt(() => method[sym](2, 3)), 5, "computed method with arguments");
check(attempt(() => method[anon]()), "anon", "symbol without a description");

class K { [sym]() { return "class " + this.tag; } }
const k = new K();
k.tag = "k";
check(attempt(() => k[sym]()), "class k", "class method found on the prototype");

// Optional call and the inherited case.
const child = Object.create(method);
check(attempt(() => child[sym](1, 1)), 2, "inherited symbol method");
check(attempt(() => child[Symbol("other")]?.()), undefined, "optional call of a missing symbol method");

if (failures) { console.log(failures + " failure(s)"); process.exit(1); }
console.log("symbol_keyed_calls: OK");
