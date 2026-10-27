// escape / unescape (ECMA-262 Annex B, §B.2.1.1-2), over UTF-16 code units.
// They were not defined. Expected values are Node.js 22's.
// Asserting test: exits 1 on any failure.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + JSON.stringify(got) + ", expected " + JSON.stringify(expected));
}
check("typeof escape", typeof escape, "function");
check("escape.length", escape.length, 1);
check("unescape.length", unescape.length, 1);
check("ascii", escape("abc ABC 123 @*_+-./"), "abc%20ABC%20123%20@*_+-./");
check("reserved", escape("!\"#$%&'(),:;<=>?[\\]^`{|}~"), "%21%22%23%24%25%26%27%28%29%2C%3A%3B%3C%3D%3E%3F%5B%5C%5D%5E%60%7B%7C%7D%7E");
check("latin-1", escape("éÿ"), "%E9%FF");
check("BMP", escape("€中"), "%u20AC%u4E2D");
check("pair", escape("\u{1F600}"), "%uD83D%uDE00");
check("lone surrogate", escape("\uD800x"), "%uD800x");
check("NUL", escape("\0"), "%00");
check("ToString", escape(12.5) + escape(null) + escape(undefined), "12.5nullundefined");
check("unescape %XX", unescape("%41%62%e9"), "Abé");
check("unescape %uXXXX", unescape("%u20ac%u20AC"), "€€");
check("unescape pair", unescape("%uD83D%uDE00") === "\u{1F600}", true);
check("unescape pair length", unescape("%uD83D%uDE00").length, 2);
check("unescape lone", unescape("%uD800").charCodeAt(0), 0xD800);
check("unescape malformed", unescape("%u12%zz%4%"), "%u12%zz%4%");
check("unescape tail", unescape("a%4"), "a%4");
check("round trip", unescape(escape("héllo \u{1F600} wörld\uDC00")), "héllo \u{1F600} wörld\uDC00");

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("escape_unescape: all checks passed");
