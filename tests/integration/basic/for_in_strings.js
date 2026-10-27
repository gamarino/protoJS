// for-in over a string lists its indices "0".."n-1", and over a String object
// its indices first, then its other own enumerable keys, then inherited ones
// (§14.7.5.6 EnumerateObjectProperties, §10.4.3.3 [[OwnPropertyKeys]]).
//
// for-in over a string primitive used to yield a single key, "stringIterNext":
// an internal method that String.prototype exposed as an enumerable property.
//
// Expected values are Node.js's (v22). Asserting test: exits 1 on any failure.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + JSON.stringify(got) + ", expected " + JSON.stringify(expected));
}
function keys(x) { var r = []; for (var k in x) r.push(k); return r.join(","); }

check("primitive", keys("abc"), "0,1,2");
check("empty", keys(""), "");
check("one character", keys("x"), "0");
check("key type", (function () { for (var k in "ab") return typeof k; })(), "string");
check("value through the key", (function () { var s = "xyz", r = ""; for (var k in s) r += s[k]; return r; })(), "xyz");

var so = new String("abc");
so.p = 1;
so[5] = "q";
check("String object: indices first", keys(so), "0,1,2,5,p");
check("Object(string)", keys(Object("hi")), "0,1");

String.prototype.inherited = 1;
check("inherited enumerable", keys("ab"), "0,1,inherited");
delete String.prototype.inherited;

check("no stringIterNext on String.prototype", "stringIterNext" in String.prototype, false);
check("no stringIterNext own name",
      Object.getOwnPropertyNames(String.prototype).indexOf("stringIterNext"), -1);

var long = "x".repeat(1000), n = 0, last = null;
for (var k in long) { n++; last = k; }
check("long string count", n, 1000);
check("long string last", last, "999");

check("Object.keys of a string", Object.keys("abc").join(), "0,1,2");
check("for-of unaffected", (function () { var r = []; for (var c of "ab") r.push(c); return r.join(); })(), "a,b");

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("for_in_strings: all checks passed");
