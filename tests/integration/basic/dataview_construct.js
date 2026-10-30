// new DataView(buffer, byteOffset, byteLength) (ECMA-262 §25.3.2.1) and
// ArrayBuffer.isView of a DataView.  Until 2026-10 DataView had no
// [[Construct]]: `new DataView(ab)` was an empty object, so dv.buffer was a
// function, every get/set method failed and ArrayBuffer.isView(dv) was false.
// Asserting test: exits 1 on any failure.  Every expected value is Node's.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + String(got) + ", expected " + String(expected));
}
function throwsType(name, f, type) {
    try { f(); failures.push(name + ": did not throw"); }
    catch (e) { if (!(e instanceof type)) failures.push(name + ": threw " + e); }
}

var ab = new ArrayBuffer(8);
var dv = new DataView(ab, 2, 4);
check("ArrayBuffer.isView(dataView)", ArrayBuffer.isView(dv), true);
check("ArrayBuffer.isView(typed array)", ArrayBuffer.isView(new Uint8Array(1)), true);
check("ArrayBuffer.isView(arrayBuffer)", ArrayBuffer.isView(ab), false);
check("ArrayBuffer.isView({})", ArrayBuffer.isView({}), false);
check("instanceof DataView", dv instanceof DataView, true);
check("toString tag", Object.prototype.toString.call(dv), "[object DataView]");
check("buffer", dv.buffer, ab);
check("byteOffset", dv.byteOffset, 2);
check("byteLength", dv.byteLength, 4);
dv.setUint8(0, 7);
check("setUint8 writes the buffer at the offset", new Uint8Array(ab)[2], 7);
check("getUint8", dv.getUint8(0), 7);
dv.setInt16(1, -2);   // big-endian by default
check("setInt16 big-endian", new Uint8Array(ab)[3] + "," + new Uint8Array(ab)[4], "255,254");
check("getInt16 little-endian", dv.getInt16(1, true), -257);
var whole = new DataView(ab);
check("default offset", whole.byteOffset, 0);
check("default length", whole.byteLength, 8);
check("offset only", new DataView(ab, 5).byteLength, 3);
check("offset at the end", new DataView(ab, 8).byteLength, 0);
check("accessor, not data", typeof Object.getOwnPropertyDescriptor(DataView.prototype, "byteLength").get, "function");
throwsType("buffer not an ArrayBuffer", function () { new DataView({}); }, TypeError);
throwsType("no buffer", function () { new DataView(); }, TypeError);
throwsType("offset past the end", function () { new DataView(ab, 9); }, RangeError);
throwsType("length past the end", function () { new DataView(ab, 4, 5); }, RangeError);
throwsType("negative offset", function () { new DataView(ab, -1); }, RangeError);
throwsType("getter on a non-DataView", function () { return DataView.prototype.byteLength; }, TypeError);

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("dataview_construct: all checks passed");
