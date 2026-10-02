// Dispatch-bound benchmark: a tight integer loop on local variables, no
// allocation and no calls inside the loop.
//
// Measures the interpreter's opcode dispatch (docs/PERFORMANCE_DISPATCH.md). It
// reports the work it did and checks it: a run that computed the wrong sum, or
// stopped early, exits 1 instead of reporting a time. The loop is inside a
// function on purpose: top-level `var`s are properties of the global object,
// which would measure property writes rather than dispatch.
function run(n) {
    var sum = 0;
    for (var i = 0; i < n; i++) {
        sum += i;
    }
    return sum;
}
var N = 20000000;
var sum = run(N);
var expected = N * (N - 1) / 2;
if (sum !== expected) {
    console.log("loop_sum: FAIL, sum " + sum + " expected " + expected);
    process.exit(1);
}
console.log("loop_sum: " + N + " iterations, sum " + sum + " (verified)");
