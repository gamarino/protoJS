// Dispatch-bound benchmark: recursive calls (every call is a runBytecode frame).
//
// Measures call and return dispatch (docs/PERFORMANCE_DISPATCH.md). It reports
// and checks the value it computed: a wrong result exits 1.
function fib(n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
var N = 30;
var r = fib(N);
var expected = 832040;
if (r !== expected) {
    console.log("call_fib: FAIL, fib(" + N + ") = " + r + " expected " + expected);
    process.exit(1);
}
console.log("call_fib: fib(" + N + ") = " + r + " (verified, 2692537 calls)");
