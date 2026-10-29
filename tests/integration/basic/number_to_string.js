// ToString(Number) is Number::toString (ECMA-262 §6.1.6.1.20): the shortest
// decimal that reads back as the same double, laid out as integer digits
// padded with zeros up to 21 digits, a decimal point, 0.000ddd down to 1e-6,
// and exponential form beyond.  Every path that turns a Number into text must
// give the same string: String(), toString(), template literals, string
// concatenation, Array.prototype.join, JSON.stringify, property keys and
// console.log.
//
// Until 2026-10 an integral double beyond 2^53 was printed as its exact
// integer value by most of those paths (2147483647 * 2147483647 printed
// 4611686014132420608, where the spec and Node give 4611686014132420600), and
// several of them used printf's %.15g (JSON.stringify(0.1 + 0.2) gave "0.3").
// A numeric literal beyond 2^53 was also loaded as a protoCore integer rather
// than a double.
// Asserting test: exits 1 on any failure.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + JSON.stringify(got) + ", expected " + JSON.stringify(expected));
}

// Every way of turning v into a string must give `expected`.
function all(name, v, expected) {
    check(name + " String()", String(v), expected);
    check(name + " toString()", v.toString(), expected);
    check(name + " toString(10)", v.toString(10), expected);
    check(name + " template", `${v}`, expected);
    check(name + " concatenation", v + "", expected);
    check(name + " join", [v].join(), expected);
    check(name + " key", Object.keys({ [v]: 1 })[0], expected);
    check(name + " String.prototype.concat", "".concat(v), expected);
    check(name + " Error message", new Error(v).message, expected);
    if (isFinite(v)) check(name + " JSON.stringify", JSON.stringify(v), expected);
    if (isFinite(v)) check(name + " JSON.stringify in array", JSON.stringify([v]), "[" + expected + "]");
}

var x = 2147483647, y = 2147483647;
all("2147483647 * 2147483647", x * y, "4611686014132420600");
all("literal 2147483647 * 2147483647", 2147483647 * 2147483647, "4611686014132420600");
all("literal 4611686014132420609", 4611686014132420609, "4611686014132420600");
check("product value", x * y === 4611686014132420608, true);
check("product is a Number", typeof (x * y), "number");

// The safe-integer boundary: exact up to 2^53, shortest digits beyond.
all("2^53 - 1", Number.MAX_SAFE_INTEGER, "9007199254740991");
all("2^53", 2 ** 53, "9007199254740992");
all("2^53 + 2", 2 ** 53 + 2, "9007199254740994");
all("-(2^53)", -(2 ** 53), "-9007199254740992");
all("2^54", 2 ** 54, "18014398509481984");
all("2^60", 2 ** 60, "1152921504606847000");
all("-(2^60)", -(2 ** 60), "-1152921504606847000");
all("2^63", 2 ** 63, "9223372036854776000");
all("2^64", 2 ** 64, "18446744073709552000");
all("1e20 + 2^16", 123456789012345680000, "123456789012345680000");
all("1e21 - 2^17", 1e21 - 131072, "999999999999999900000");
all("1e21", 1e21, "1e+21");
all("2^70", 2 ** 70, "1.1805916207174113e+21");
all("-1e21", -1e21, "-1e+21");
all("1.5e300", 1.5e300, "1.5e+300");
all("MAX_VALUE", Number.MAX_VALUE, "1.7976931348623157e+308");

// Fractions and small numbers.
all("0.1 + 0.2", 0.1 + 0.2, "0.30000000000000004");
all("1.1", 1.1, "1.1");
all("-3.14", -3.14, "-3.14");
all("0.000001", 0.000001, "0.000001");
all("1e-7", 1e-7, "1e-7");
all("1.5e-7", 1.5e-7, "1.5e-7");
all("0.000012345", 0.000012345, "0.000012345");
all("123.456", 123.456, "123.456");
all("MIN_VALUE", Number.MIN_VALUE, "5e-324");
all("2^-1074 * 3", 3 * 2 ** -1074, "1.5e-323");
all("1/3", 1 / 3, "0.3333333333333333");
all("2/3", 2 / 3, "0.6666666666666666");
all("1e21 / 3", 1e21 / 3, "333333333333333300000");

// Zero, non-finite.
all("-0", -0, "0");
all("0", 0, "0");
all("NaN", NaN, "NaN");
all("Infinity", Infinity, "Infinity");
all("-Infinity", -Infinity, "-Infinity");
check("JSON.stringify(NaN)", JSON.stringify(NaN), "null");

// Small integers stay plain.
all("42", 42, "42");
all("-7", -7, "-7");

// The digits read back as the same double.  (Not 5e-324: Number("5e-324")
// gives Infinity, a defect of StringToNumber for subnormals, not of this.)
var samples = [x * y, 2 ** 63, 0.1 + 0.2, 1 / 3, 1e21 / 7, 1.7976931348623157e308, 123456.789e10];
for (var v of samples) check("round trip " + v, Number(String(v)), v);

// Typed array join formats elements the same way.
check("Float64Array join", new Float64Array([x * y, 0.1 + 0.2, 1e-7]).join(), "4611686014132420600,0.30000000000000004,1e-7");

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("number_to_string: all checks passed");
