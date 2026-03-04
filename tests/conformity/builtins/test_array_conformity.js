// Phase 1.1.3 protoJS: Array — push, extend, index; result is new root in scope.

console.log("Testing Array conformity...");

const arr = [];
arr.push(1);
arr.push(2);
if (arr.length !== 2 || arr[0] !== 1 || arr[1] !== 2) throw new Error("push");

arr.push(3, 4);
if (arr.length !== 4) throw new Error("push multiple");

arr[0] = 10;
if (arr[0] !== 10) throw new Error("index assign");

const sub = arr.slice(1, 3);
if (sub.length !== 2 || sub[0] !== 2) throw new Error("slice");

console.log("OK Array conformity");
