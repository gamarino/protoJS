// Numbers produced by built-ins and by arithmetic keep exact JavaScript
// semantics whatever protoCore representation holds them (a tagged
// SmallInteger or a boxed double), and protoJS stores integral results in
// the canonical SmallInteger form (src/JSNumber.h).
//
// The semantic checks run unchanged under Node.js (`node <this file>`), which
// validates the expected values; the representation checks run only where
// protoCore.isSmallInteger exists.

let failures = 0;
let checks = 0;
function check(name, cond) {
    checks++;
    if (!cond) { failures++; console.log("FAILED: " + name); }
}
function isNegZero(v) { return v === 0 && 1 / v === -Infinity; }

const str42 = "42";
const dbl = 2.5;

// name -> [value, expected]
const integral = {
    "Math.ceil(2.5)": [Math.ceil(dbl), 3],
    "Math.floor(2.5)": [Math.floor(dbl), 2],
    "Math.round(2.5)": [Math.round(dbl), 3],
    "Math.trunc(-2.5)": [Math.trunc(-dbl), -2],
    "Math.abs(-3)": [Math.abs(-3), 3],
    "Math.max(1, 2.0)": [Math.max(1, 2.0), 2],
    "Math.min(4, 1.5 * 2)": [Math.min(4, 1.5 * 2), 3],
    "Math.pow(2, 10)": [Math.pow(2, 10), 1024],
    "2 ** 10": [2 ** 10, 1024],
    "Math.sqrt(16)": [Math.sqrt(16), 4],
    "Math.hypot(3, 4)": [Math.hypot(3, 4), 5],
    "Number('42')": [Number(str42), 42],
    "Number('1e3')": [Number("1e3"), 1000],
    "Number('3.0')": [Number("3.0"), 3],
    "Number(true)": [Number(true), 1],
    "+'42'": [+str42, 42],
    "'42' * 1": [str42 * 1, 42],
    "parseInt('42')": [parseInt(str42), 42],
    "parseFloat('42')": [parseFloat(str42), 42],
    "parseFloat('3.0')": [parseFloat("3.0"), 3],
    "Number.parseFloat('5')": [Number.parseFloat("5"), 5],
    "JSON.parse('42')": [JSON.parse("42"), 42],
    "JSON.parse('4.0')": [JSON.parse("4.0"), 4],
    "JSON.parse('{\"x\":1e3}').x": [JSON.parse('{"x":1e3}').x, 1000],
    "6 / 2": [6 / 2, 3],
    "7 % 3": [7 % 3, 1],
    "1.5 * 2": [1.5 * 2, 3],
    "0.5 + 0.5": [0.5 + 0.5, 1],
    "2.5 - 0.5": [2.5 - 0.5, 2],
    "2.5 | 0": [dbl | 0, 2],
    "-1 >>> 0": [-1 >>> 0, 4294967295],
    "[1, 2, 3].length": [[1, 2, 3].length, 3],
    "Math.floor(2.5) + 1": [Math.floor(dbl) + 1, 3],
    "Math.floor(2.5) * 2": [Math.floor(dbl) * 2, 4],
    "++ on Math.floor(2.5)": [(() => { let x = Math.floor(dbl); x++; return x; })(), 3],
    "-- on 3.5 - 0.5": [(() => { let x = 3.5 - 0.5; --x; return x; })(), 2],
    "-(1.5 * 2)": [-(1.5 * 2), -3],
    "acc += 0.5 twice": [(() => { let a = 1; a += 0.5; a += 0.5; return a; })(), 2],
    "Number.MAX_SAFE_INTEGER": [Number.MAX_SAFE_INTEGER, 9007199254740991],
    "new Date(86400000).getTime()": [new Date(86400000).getTime(), 86400000],
};

const hasProbe = typeof protoCore === "object" && protoCore !== null
    && typeof protoCore.isSmallInteger === "function";

for (const name in integral) {
    const [v, expected] = integral[name];
    check(name + " === " + expected, v === expected);
    check(name + " typeof", typeof v === "number");
    check(name + " Object.is", Object.is(v, expected));
    check(name + " Number.isInteger", Number.isInteger(v));
    check(name + " String", String(v) === String(expected));
    check(name + " as Map key", new Map([[expected, "k"]]).get(v) === "k");
    check(name + " as Set member", new Set([v]).has(expected));
    check(name + " as property key", ({ [expected]: "p" })[v] === "p");
    if (hasProbe) check(name + " is a SmallInteger", protoCore.isSmallInteger(v));
}

// -0 stays -0: every producer that must yield -0 does, and it is not 0 for
// Object.is.
const negZero = {
    "Math.round(-0.4)": Math.round(-0.4),
    "Math.ceil(-0.5)": Math.ceil(-0.5),
    "Math.trunc(-0.5)": Math.trunc(-0.5),
    "-0 literal": -0,
    "0 * -5": 0 * -5,
    "-5 * 0": -5 * 0,
    "-4 % 2": -4 % 2,
    "-1.5 % 0.5": -1.5 % 0.5,
    "-0 + -0": -0 + -0,
    "1 / -Infinity": 1 / -Infinity,
    "(-0) ** 3": (-0) ** 3,
    "Number('-0')": Number("-0"),
    "JSON.parse('-0')": JSON.parse("-0"),
    "-(0)": -(0),
    "Math.min(0, -0)": Math.min(0, -0),
    "Math.max(-0, -0)": Math.max(-0, -0),
};
for (const name in negZero) {
    const v = negZero[name];
    check(name + " is -0", isNegZero(v));
    check(name + " Object.is(v, -0)", Object.is(v, -0));
    check(name + " !Object.is(v, 0)", !Object.is(v, 0));
    check(name + " === 0", v === 0);
    if (hasProbe) check(name + " is not a SmallInteger", !protoCore.isSmallInteger(v));
}
check("Object.is(-0, 0) is false", Object.is(-0, 0) === false);
check("4 % 2 is +0", Object.is(4 % 2, 0));
check("0 * 5 is +0", Object.is(0 * 5, 0));

// Non-integral and non-finite values stay doubles.
const notInt = {
    "NaN": [NaN, (v) => Number.isNaN(v)],
    "0 / 0": [0 / 0, (v) => Number.isNaN(v)],
    "Number('x')": [Number("x"), (v) => Number.isNaN(v)],
    "1 / 0": [1 / 0, (v) => v === Infinity],
    "-1 / 0": [-1 / 0, (v) => v === -Infinity],
    "5 / 2": [5 / 2, (v) => v === 2.5],
    "Math.sqrt(2) ** 2": [Math.sqrt(2) ** 2, (v) => v === 2.0000000000000004],
};
for (const name in notInt) {
    const [v, ok] = notInt[name];
    check(name + " value", ok(v));
    if (hasProbe) check(name + " is not a SmallInteger", !protoCore.isSmallInteger(v));
}

// Beyond 2^53 integers are doubles and round like doubles.
const big = 9007199254740992;  // 2 ** 53
check("2**53 + 1 rounds to 2**53", big + 1 === big);
check("2**53 + 2 is exact", big + 2 === 9007199254740994);
check("MAX_SAFE_INTEGER + 2 === 2**53 + 2 (rounded)",
      Number.MAX_SAFE_INTEGER + 2 === 9007199254740992);
check("2**53 * 2 === 2**54", big * 2 === 18014398509481984);
check("94906267 * 94906267 rounds", 94906267 * 94906267 === 9007199515875288);
check("2**60 is a number", typeof (2 ** 60) === "number" && 2 ** 60 === 1152921504606846976);
check("1e21 formats", String(1e21) === "1e+21");
if (hasProbe) {
    check("2**53 is not a SmallInteger", !protoCore.isSmallInteger(big));
    check("2**53 + 1 is not a SmallInteger", !protoCore.isSmallInteger(big + 1));
    check("MAX_SAFE_INTEGER is a SmallInteger", protoCore.isSmallInteger(Number.MAX_SAFE_INTEGER));
}

// Mixed representations compare and hash alike.
const viaDouble = Math.floor(7.9);
const viaInt = 7;
check("=== across producers", viaDouble === viaInt);
check("Map keyed by producer", new Map([[viaDouble, 1]]).has(viaInt));
check("array index via Math.floor", ["a", "b"][Math.floor(1.5)] === "b");
check("switch on Math.ceil", (() => { switch (Math.ceil(0.5)) { case 1: return true; default: return false; } })());

// Integer keys outside the array-index range in object literals.
check("{[-2]: v} keys", JSON.stringify(Object.keys({ [-2]: 1 })) === '["-2"]');
check("{[2**40]: v} keys", JSON.stringify(Object.keys({ [2 ** 40]: 1 })) === '["1099511627776"]');
check("{[2**40]: v} read", ({ [2 ** 40]: "x" })["1099511627776"] === "x");

if (failures === 0) {
    console.log("number_representation: all " + checks + " checks passed");
} else {
    console.log("number_representation: " + failures + " of " + checks + " checks FAILED");
    if (typeof process !== "undefined") process.exit(1);
}
