// Phase 1.1.4 protoJS: Object — get/set/delete, iteration; new root propagated to owner.

console.log("Testing Object conformity...");

const obj = {};
obj.a = 1;
obj.b = 2;
if (obj.a !== 1 || obj.b !== 2) throw new Error("set/get");

const keys = Object.keys(obj);
if (keys.length !== 2 || !keys.includes("a") || !keys.includes("b")) throw new Error("keys");

delete obj.a;
if ("a" in obj || obj.a !== undefined) throw new Error("delete");
if (obj.b !== 2) throw new Error("after delete");

console.log("OK Object conformity");
