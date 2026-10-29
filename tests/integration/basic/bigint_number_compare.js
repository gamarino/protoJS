// BigInt against Number by mathematical value (§7.2.13 IsLessThan, §7.2.15
// IsLooselyEqual), exact at every magnitude, and NumberToBigInt exact for any
// integral Number.
//
// Until 2026-10 BigInt == Number formatted the Number through a long long
// cast (undefined beyond 2^63), so 2n ** 64n == 2 ** 64 was false; the
// relational operators compared the BigInt's rounded double beyond 2^63, so
// 2n ** 64n + 1n > 2 ** 64 was false; and BigInt(2 ** 64) was garbage.
// Asserting test: exits 1 on any failure.

var failures = [];
function check(name, got, expected) {
    // === for BigInts (Object.is on two equal BigInts is a separate defect).
    var same = typeof got === "bigint" ? got === expected : Object.is(got, expected);
    if (!same) failures.push(name + ": got " + String(got) + ", expected " + String(expected));
}
// All six relations of a BigInt b and a Number n whose order is `order`
// (-1, 0, 1; NaN for unordered), both ways round.
function rel(name, b, n, order) {
    var lt = order === -1, eq = order === 0, gt = order === 1;
    check(name + " ==", b == n, eq);
    check(name + " != ", b != n, !eq);
    check(name + " <", b < n, lt);
    check(name + " <=", b <= n, lt || eq);
    check(name + " >", b > n, gt);
    check(name + " >=", b >= n, gt || eq);
    check(name + " (swapped) ==", n == b, eq);
    check(name + " (swapped) <", n < b, gt);
    check(name + " (swapped) >", n > b, lt);
    check(name + " (swapped) <=", n <= b, gt || eq);
    check(name + " (swapped) >=", n >= b, lt || eq);
    check(name + " ===", b === n, false);
}

var p53 = 2n ** 53n, p63 = 2n ** 63n, p64 = 2n ** 64n;

// Small and safe magnitudes.
rel("0n vs 0", 0n, 0, 0);
rel("0n vs -0", 0n, -0, 0);
rel("1n vs 1", 1n, 1, 0);
rel("-1n vs -1", -1n, -1, 0);
rel("1n vs 1.5", 1n, 1.5, -1);
rel("2n vs 1.5", 2n, 1.5, 1);
rel("-1n vs -1.5", -1n, -1.5, 1);
rel("-2n vs -1.5", -2n, -1.5, -1);
rel("5n vs 5.000000000000001", 5n, 5.000000000000001, -1);

// NaN and the infinities.
rel("0n vs NaN", 0n, NaN, NaN);
rel("p64 vs NaN", p64, NaN, NaN);
rel("p64 vs Infinity", p64, Infinity, -1);
rel("p64 vs -Infinity", p64, -Infinity, 1);
rel("2n ** 1024n vs Infinity", 2n ** 1024n, Infinity, -1);
rel("-(2n ** 1024n) vs -Infinity", -(2n ** 1024n), -Infinity, 1);

// Around 2^53, where a double stops holding every integer.
rel("p53 vs 2 ** 53", p53, 2 ** 53, 0);
rel("p53 + 1n vs 2 ** 53", p53 + 1n, 2 ** 53, 1);
rel("p53 + 1n vs 2 ** 53 + 2", p53 + 1n, 2 ** 53 + 2, -1);
rel("p53 - 1n vs 2 ** 53 - 1", p53 - 1n, 2 ** 53 - 1, 0);

// Around and beyond 2^63 / 2^64, past long long.
rel("p63 vs 2 ** 63", p63, 2 ** 63, 0);
rel("p63 vs 9223372036854775807 (the double 2^63)", p63, 9223372036854775807, 0);
rel("p63 - 1n vs 2 ** 63", p63 - 1n, 2 ** 63, -1);
rel("-p63 vs -(2 ** 63)", -p63, -(2 ** 63), 0);
rel("-p63 - 1n vs -(2 ** 63)", -p63 - 1n, -(2 ** 63), -1);
rel("p64 vs 2 ** 64", p64, 2 ** 64, 0);
rel("p64 + 1n vs 2 ** 64", p64 + 1n, 2 ** 64, 1);
rel("p64 - 1n vs 2 ** 64", p64 - 1n, 2 ** 64, -1);
rel("-p64 vs -(2 ** 64)", -p64, -(2 ** 64), 0);
rel("-p64 - 1n vs -(2 ** 64)", -p64 - 1n, -(2 ** 64), -1);
rel("2n ** 100n vs 2 ** 100", 2n ** 100n, 2 ** 100, 0);
rel("2n ** 100n + 1n vs 2 ** 100", 2n ** 100n + 1n, 2 ** 100, 1);
rel("3n * 2n ** 70n vs 3 * 2 ** 70", 3n * 2n ** 70n, 3 * 2 ** 70, 0);
rel("1e21 as BigInt vs 1e21", 1000000000000000000000n, 1e21, 0);
rel("1e21 + 1 vs 1e21", 1000000000000000000001n, 1e21, 1);
rel("MAX_VALUE as BigInt vs MAX_VALUE", BigInt(Number.MAX_VALUE), Number.MAX_VALUE, 0);
rel("MAX_VALUE as BigInt + 1n vs MAX_VALUE", BigInt(Number.MAX_VALUE) + 1n, Number.MAX_VALUE, 1);
rel("2n ** 1024n vs MAX_VALUE", 2n ** 1024n, Number.MAX_VALUE, 1);
rel("2n ** 64n vs 2 ** 64 + 4096", p64, 2 ** 64 + 4096, -1);

// NumberToBigInt: exact for every integral Number.
check("BigInt(2 ** 53)", BigInt(2 ** 53), p53);
check("BigInt(2 ** 63)", BigInt(2 ** 63), p63);
check("BigInt(-(2 ** 63))", BigInt(-(2 ** 63)), -p63);
check("BigInt(2 ** 64)", BigInt(2 ** 64), p64);
check("BigInt(2 ** 64) string", String(BigInt(2 ** 64)), "18446744073709551616");
check("BigInt(1e21)", BigInt(1e21), 1000000000000000000000n);
check("BigInt(1e300) length", String(BigInt(1e300)).length, 301);
check("BigInt(MAX_VALUE) bits", BigInt(Number.MAX_VALUE) >> 971n, 2n ** 53n - 1n);
check("BigInt(-1e300) sign", BigInt(-1e300) < 0n, true);
check("Number(BigInt(1e300))", Number(BigInt(1e300)), 1e300);

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("bigint_number_compare: all checks passed");
