// StringToNumber (ECMA-262 §7.1.4.1.1) gives the string's value rounded to
// the nearest double: a subnormal for "5e-324", ±0 below half the smallest
// subnormal, ±Infinity beyond the largest double; hexadecimal, octal and
// binary literals rounded the same way at any length.
//
// Until 2026-10 Number("5e-324") was Infinity (std::stod's out_of_range
// covers underflow as well as overflow, and both became ±Infinity), and
// Number("0xFFFFFFFFFFFFFFFF") was -1 (strtoull, cast to long long).
// Asserting test: exits 1 on any failure.

var failures = [];
function check(name, got, expected) {
    if (!Object.is(got, expected)) failures.push(name + ": got " + got + ", expected " + expected);
}

// Every ToNumber of a string must agree: Number(), unary plus, arithmetic,
// comparison, and parseFloat where the grammar overlaps.
function all(text, expected) {
    check("Number(" + JSON.stringify(text) + ")", Number(text), expected);
    check("+" + JSON.stringify(text), +text, expected);
    check(JSON.stringify(text) + " * 1", text * 1, expected);
    check(JSON.stringify(text) + " - 0", text - 0, expected);
    check("new Number(" + JSON.stringify(text) + ")", new Number(text).valueOf(), expected);
}

// Subnormals and the extremes.
all("5e-324", 5e-324);
all("4.9406564584124654e-324", 5e-324);
all("2.4703282292062328e-324", 5e-324);     // just above half the minimum: rounds up
all("2.4703282292062327e-324", 0);          // just below half: rounds to zero
all("1e-310", 1e-310);
all("2.2250738585072009e-308", 2.2250738585072009e-308);  // largest subnormal
all("2.2250738585072014e-308", 2.2250738585072014e-308);  // smallest normal
all("-5e-324", -5e-324);
all("1e-400", 0);
all("-1e-400", -0);
all("0e-999", 0);
all("1.7976931348623157e308", 1.7976931348623157e308);
all("1.7976931348623158e308", 1.7976931348623157e308);   // rounds down to MAX_VALUE
all("1.7976931348623159e308", Infinity);                 // rounds up past it
all("1e309", Infinity);
all("-1e309", -Infinity);
all("10e10000", Infinity);
all("-10e10000", -Infinity);
check("Number.MIN_VALUE round trip", Number(String(Number.MIN_VALUE)), Number.MIN_VALUE);
check("Number.MAX_VALUE round trip", Number(String(Number.MAX_VALUE)), Number.MAX_VALUE);
check("parseFloat agrees on 5e-324", parseFloat("5e-324"), Number("5e-324"));
check("parseFloat agrees on 1e-400", parseFloat("1e-400"), Number("1e-400"));

// Ordinary decimals still parse; invalid forms are NaN.
all("  42  ", 42);
all("-0", -0);
all("0.1", 0.1);
all(".5", 0.5);
all("5.", 5);
all("1e3", 1000);
all("1E+3", 1000);
all("", 0);
all("Infinity", Infinity);
all("-Infinity", -Infinity);
all("12abc", NaN);
all("1e", NaN);
all("infinity", NaN);
all("0x1p3", NaN);

// Hexadecimal, octal and binary literals, rounded at any length.
all("0x10", 16);
all("0XfF", 255);
all("0o17", 15);
all("0b101", 5);
all("0x1fffffffffffff", 9007199254740991);
all("0x20000000000001", 9007199254740992);       // 2^53 + 1: ties to even
all("0x20000000000003", 9007199254740996);       // 2^53 + 3: ties to even (up)
all("0xFFFFFFFFFFFFFFFF", 18446744073709552000);
all("0x10000000000000000", 18446744073709552000);
all("0b" + "1".repeat(64), 18446744073709552000);
all("0o" + "7".repeat(30), 1.2379400392853803e27);
all("0x" + "f".repeat(256), Infinity);
all("-0x10", NaN);
all("0x", NaN);
all("0xg", NaN);
all("0b2", NaN);
all("0o8", NaN);

// Paths that convert a string length or argument through ToNumber.
check("array-like length '5e-324'", Array.prototype.indexOf.call({ length: "5e-324", 0: 1 }, 1), -1);
check("array-like length '0x2'", Array.prototype.indexOf.call({ length: "0x2", 1: 7 }, 7), 1);
check("flat depth '1e-400'", [[1, [2]]].flat("1e-400").length, 1);
check("flat depth ' 2 '", [[1, [2]]].flat(" 2 ").length, 2);

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("string_to_number: all checks passed");
