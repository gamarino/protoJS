// A Buffer is a Uint8Array (Node): Buffer.prototype inherits from
// Uint8Array.prototype and every Buffer is a typed array over an ArrayBuffer,
// so it is indexable, an instance of Uint8Array, "[object Uint8Array]" to
// Object.prototype.toString, and buf.slice / buf.subarray share its memory.
// Until 2026-10 a Buffer kept its bytes in a hidden attribute: buf[0] was
// undefined and none of the above held.
//
// Asserting test: exits 1 on any failure.  Every expected value is Node's.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + String(got) + ", expected " + String(expected));
}

var b = Buffer.from("hello");
check("toString tag", Object.prototype.toString.call(b), "[object Uint8Array]");
check("buf instanceof Uint8Array", b instanceof Uint8Array, true);
check("buf instanceof Buffer", b instanceof Buffer, true);
check("Buffer.prototype parent", Object.getPrototypeOf(Buffer.prototype), Uint8Array.prototype);
check("ArrayBuffer.isView(buf)", ArrayBuffer.isView(b), true);
check("buf.buffer instanceof ArrayBuffer", b.buffer instanceof ArrayBuffer, true);
check("length", b.length, 5);
check("byteLength", b.byteLength, 5);
check("buf[0]", b[0], 104);
check("buf[4]", b[4], 111);
check("buf[5]", b[5], undefined);
check("Object.keys", Object.keys(b).join(), "0,1,2,3,4");
b[0] = 72;
check("index write", b.toString(), "Hello");
b[1] = 0x165;   // stored modulo 256, as in every Uint8Array
check("index write wraps", b[1], 0x65);
check("Array.from(buf)", Array.from(Buffer.from([1, 2, 3])).join(), "1,2,3");
var sum = 0;
for (var x of Buffer.from([1, 2, 3])) sum += x;
check("for-of", sum, 6);
check("map is a typed-array method", typeof b.map, "function");
check("Buffer.isBuffer(new Uint8Array(2))", Buffer.isBuffer(new Uint8Array(2)), false);
check("Buffer.isBuffer(buf)", Buffer.isBuffer(b), true);

// slice and subarray are views over the same memory.
var whole = Buffer.from("abcdef");
var part = whole.slice(1, 3);
check("slice content", part.toString(), "bc");
check("slice is a Buffer", Buffer.isBuffer(part), true);
part[0] = 0x58;   // "X"
check("slice shares memory", whole.toString(), "aXcdef");
var sub = whole.subarray(4);
check("subarray content", sub.toString(), "ef");
check("subarray byteOffset", sub.byteOffset - whole.byteOffset, 4);
sub.fill("z");
check("subarray fill shares memory", whole.toString(), "aXcdzz");
check("indexOf on a view", whole.subarray(2).indexOf("d"), 1);

// Buffer.from(arrayBuffer, offset, length) is a view over that memory.
var ab = new ArrayBuffer(4);
var view = Buffer.from(ab, 1, 2);
view[0] = 7;
check("Buffer.from(arrayBuffer) shares memory", new Uint8Array(ab)[1], 7);
check("Buffer.from(arrayBuffer) length", view.length, 2);
check("Buffer.from(arrayBuffer) buffer", view.buffer, ab);

// Typed arrays are accepted where Node accepts them.
check("Buffer.from(Uint8Array)", Buffer.from(new Uint8Array([65, 66])).toString(), "AB");
check("Buffer.from(Uint16Array) takes elements mod 256", Buffer.from(new Uint16Array([0x141, 0x42])).toString(), "AB");
check("Buffer.concat with a Uint8Array", Buffer.concat([Buffer.from("x"), new Uint8Array([121])]).toString(), "xy");
var target = Buffer.alloc(4);
check("copy count", Buffer.from("hey").copy(target, 1), 3);
check("copy content", target.toString("hex"), "00686579");
check("alloc zeroed", Buffer.alloc(3).toString("hex"), "000000");
check("new Buffer(n)", new Buffer(2).length, 2);

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("buffer_is_uint8array: all checks passed");
