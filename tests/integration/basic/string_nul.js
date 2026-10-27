// A string may contain U+0000. String literals with an embedded NUL
// ("a\0b", "a\u0000b", template literals) were cut at the NUL: the loader
// built each literal from a C string, and so did most built-ins that build a
// result from UTF-8 bytes (concat, replace, toUpperCase, join, JSON.parse
// keys, Error messages, property keys ...). Length and content must survive
// end to end.
//
// Expected values are Node.js's (v22). Property key order is protoCore's, a
// documented deviation (docs/API_REFERENCE.md), so key lists are sorted.
// Asserting test: exits 1 on any failure.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + JSON.stringify(got) + ", expected " + JSON.stringify(expected));
}

var z = "a\0b";
check("literal length", z.length, 3);
check("literal content", z.charCodeAt(1), 0);
check("literal last", z.charCodeAt(2), 98);
check("\\u0000 escape", z === "a\u0000b", true);
check("not equal to its prefix", z === "a", false);
check("template", `x\0y`.length, 3);
check("template with substitution", `x\0${1}`, "x\u00001");
check("template of NULs", `${z}${z}`.length, 6);
check("String.raw keeps the escape", String.raw`a\0b`, "a\\0b");
check("eval literal", eval('"x\\0y"').length, 3);
check("eval raw NUL", eval("'p\0q'").length, 3);
check("long literal", "abcdefghij\0klmnopqrstuvwxyz".length, 27);
check("long literal index", "abcdefghij\0klmnopqrstuvwxyz"[10], "\0");

check("String.fromCharCode(0)", String.fromCharCode(0).length, 1);
check("String.fromCodePoint", String.fromCodePoint(0, 97), "\0a");
check("index", z[1], "\0");
check("charAt", z.charAt(1), "\0");
check("at", z.at(1), "\0");
check("spread", [...z].length, 3);
check("Array.from", Array.from(z)[1], "\0");

check("concat", "".concat(z, z), "a\0ba\0b");
check("+ number", z + 1, "a\0b1");
check("number +", 1 + z, "1a\0b");
check("slice", z.slice(1), "\0b");
check("substring", z.substring(0, 2), "a\0");
check("substr", z.substr(1), "\0b");
check("split", z.split("\0").join("|"), "a|b");
check("split all", z.split("").length, 3);
check("replace", z.replace("b", "c"), "a\0c");
check("replaceAll", z.replaceAll("b", "c"), "a\0c");
check("replace regexp", z.replace(/b/, "c"), "a\0c");
check("replace global regexp", z.replace(/b/g, "c"), "a\0c");
check("regexp with NUL", /a\0b/.test(z), true);
check("regexp capture", /\0(b)/.exec(z)[1], "b");
check("toUpperCase", z.toUpperCase(), "A\0B");
check("toLowerCase", "A\0B".toLowerCase(), "a\0b");
check("repeat", "\0".repeat(3).length, 3);
check("padEnd", z.padEnd(5).length, 5);
check("padStart with NUL", z.padStart(5, "\0"), "\0\0a\0b");
check("trim keeps NUL", " \0 ".trim(), "\0");
check("indexOf", z.indexOf("\0"), 1);
check("includes", z.includes("\0b"), true);
check("startsWith", z.startsWith("a\0"), true);
check("endsWith", z.endsWith("\0b"), true);
check("compare", "a\0" < "a\0b", true);
check("join", [z, z].join("\0").length, 7);
check("String()", String(new String(z)).length, 3);
check("normalize", z.normalize().length, 3);

check("JSON.stringify", JSON.stringify(z), '"a\\u0000b"');
check("JSON.parse value", JSON.parse('"a\\u0000b"'), z);
check("JSON.parse key", Object.keys(JSON.parse('{"a\\u0000b":1}'))[0], z);
check("JSON round trip", JSON.parse(JSON.stringify({ k: z })).k, z);

var o = {};
o[z] = 1;
o.a = 2;
check("NUL key does not collide with its prefix", o[z] + o.a, 3);
check("key count", Object.keys(o).length, 2);
check("keys", Object.keys(o).sort().join("|"), "a|a\0b");
check("in", z in o, true);
check("hasOwnProperty", o.hasOwnProperty(z), true);
check("JSON of the object", JSON.stringify(o).indexOf('"a\\u0000b":1') >= 0, true);
check("Map key", new Map([[z, 1]]).get("a\0b"), 1);
check("Set member", new Set([z]).has("a\u0000b"), true);
check("Symbol description", Symbol(z).description, z);

check("encodeURIComponent", encodeURIComponent(z), "a%00b");
check("encodeURI", encodeURI(z), "a%00b");
check("decodeURIComponent", decodeURIComponent("a%00b"), z);
check("Error message", new Error(z).message, z);
check("thrown string", (function () { try { throw z; } catch (e) { return e; } })(), z);
check("returned", (function () { return z; })(), z);

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("string_nul: all checks passed");
