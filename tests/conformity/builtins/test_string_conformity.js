// Phase 1.1.2 protoJS: String — concat, slice; all return new roots.

console.log("Testing String conformity...");

if (typeof "hello" !== "string") throw new Error("string type");
if ("a" + "b" !== "ab") throw new Error("concat");
if ("hello".slice(1, 4) !== "ell") throw new Error("slice");
if ("  x  ".trim() !== "x") throw new Error("trim");
if ("a,b,c".split(",").join(",") !== "a,b,c") throw new Error("split");

const s = "original";
if (s !== "original") throw new Error("immutability");

console.log("OK String conformity");
