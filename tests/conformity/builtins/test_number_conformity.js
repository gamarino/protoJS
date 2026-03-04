// Phase 1.1.1 protoJS: Number — arithmetic, identity; no in-place mutation.
// Immutability: operations return new roots.

console.log("Testing Number conformity...");

if (typeof 42 !== "number") throw new Error("number type");
if (1 + 2 !== 3) throw new Error("add");
if (10 - 3 !== 7) throw new Error("subtract");
if (2 * 3 !== 6) throw new Error("multiply");
if (7 % 2 !== 1) throw new Error("mod");

const a = 1, b = 1;
if (a !== b) throw new Error("identity");

console.log("OK Number conformity");
