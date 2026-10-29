// Top-level console.log output of primitive values whose formatting protoJS
// shares with Node (console-values.sh compares with console_values.expected,
// Node's output). Until 2026-10 a BigInt printed as {} and -0 as 0.
console.log(5n);
console.log(-5n, 0n);
console.log(2n ** 70n);
console.log(-(2n ** 64n));
console.log(BigInt(9007199254740993n), BigInt.asUintN(64, -1n));
console.log(-0);
console.log(0, -1.5, 1e21);
console.log("x", 5n, -0, "y");
