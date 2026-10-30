// A Buffer is a JavaScript object: Buffer.prototype inherits from
// Object.prototype, so ToPrimitive finds Buffer.prototype.toString through
// the ordinary valueOf / toString order, and the Object.prototype methods
// (hasOwnProperty, valueOf, isPrototypeOf, ...) are there.
//
// Until 2026-10 Buffer.prototype was a bare protoCore object, so a Buffer was
// treated as a foreign object: `"echo:" + buf` gave "echo:[object Object]"
// and buf.valueOf / buf.hasOwnProperty were undefined.  Asserting test: exits
// 1 on any failure.  Every expected value is Node's.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + String(got) + ", expected " + String(expected));
}

var b = Buffer.from("hi");
check("'echo:' + buf", "echo:" + b, "echo:hi");
check("buf + ''", b + "", "hi");
check("template literal", `${b}`, "hi");
check("String(buf)", String(b), "hi");
check("buf == 'hi'", b == "hi", true);
check("typeof buf.valueOf", typeof b.valueOf, "function");
check("buf.valueOf() === buf", b.valueOf() === b, true);
check("typeof buf.hasOwnProperty", typeof b.hasOwnProperty, "function");
check("buf.hasOwnProperty('toString')", b.hasOwnProperty("toString"), false);
check("Buffer.prototype.hasOwnProperty('toString')", Buffer.prototype.hasOwnProperty("toString"), true);
check("Object.prototype.isPrototypeOf(buf)", Object.prototype.isPrototypeOf(b), true);
check("Buffer.prototype.isPrototypeOf(buf)", Buffer.prototype.isPrototypeOf(b), true);
check("buf instanceof Object", b instanceof Object, true);
check("buf instanceof Buffer", b instanceof Buffer, true);
check("Buffer.isBuffer(buf)", Buffer.isBuffer(b), true);
check("Buffer.isBuffer({})", Buffer.isBuffer({}), false);
check("toString('hex')", b.toString("hex"), "6869");
check("concat of two", Buffer.concat([b, Buffer.from("!")]) + "", "hi!");
check("alloc fill", "" + Buffer.alloc(3, "a"), "aaa");

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("buffer_object_prototype: all checks passed");
