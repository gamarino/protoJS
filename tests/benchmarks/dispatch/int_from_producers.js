// Integer loop whose bound, step and modulus come from number producers
// (Math.ceil, Number(string), parseInt) rather than from integer literals.
//
// Those producers used to return boxed doubles even for integral values, so
// every operation in the loop missed the interpreter's SmallInteger fast
// paths and allocated a double. They now return canonical SmallIntegers
// (src/JSNumber.h). The benchmark reports the work it did and checks it: a
// wrong sum exits 1 instead of reporting a time.
function run(n) {
    var limit = Math.ceil(n - 0.5);   // n
    var step = Number("1");
    var k = parseInt("3", 10);
    var sum = 0;
    for (var i = 0; i < limit; i += step) {
        sum += (i % k) * step;
    }
    return sum;
}
var N = 5000000;
var sum = run(N);
var r = N % 3;
var expected = (N - r) / 3 * 3 + (r === 2 ? 1 : 0);
if (sum !== expected) {
    console.log("int_from_producers: FAIL, sum " + sum + " expected " + expected);
    process.exit(1);
}
console.log("int_from_producers: " + N + " iterations, sum " + sum + " (verified)");
