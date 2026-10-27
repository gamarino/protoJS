// Regenerates the expected values of a case-list test with Node.js:
//
//   node tests/scripts/gen_node_expected.js <source.js> <test.js>
//
// <source.js> holds a header comment and the cases between the
// "// CASES-BEGIN" and "// CASES-END" lines (the test itself can be the
// source). The output is the header, the cases, the results Node.js computes
// for them and a checker that exits 1 on any difference.
const fs = require("fs");
const src = fs.readFileSync(process.argv[2], "utf8");
const body = src.slice(src.indexOf("// CASES-BEGIN"), src.indexOf("// CASES-END"));
const header = src.slice(0, src.indexOf("// CASES-BEGIN")).replace(/\/\/ Generated from the case list[^\n]*\n\n/, "");
const expected = new Function(body + "\nvar out = {}; cases.forEach(function (c) { var v; try { v = show(c[1]()); } catch (e) { v = 'throws ' + e.name; } out[c[0]] = v; }); return out;")();
let out = header;
out += "// Generated from the case list by Node.js " + process.version + "; asserting test, exits 1 on any failure.\n\n";
out += body + "// CASES-END\n";
out += "\nvar expected = " + JSON.stringify(expected, null, 0).replace(/","/g, '",\n"') + ";\n\n";
out += `var failures = [];
cases.forEach(function (c) {
    var got;
    try { got = show(c[1]()); } catch (e) { got = "throws " + (e && e.name); }
    if (got !== expected[c[0]]) failures.push(c[0] + "\\n   got      " + got + "\\n   expected " + expected[c[0]]);
});
if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    console.log(failures.length + " of " + cases.length + " cases failed");
    process.exit(1);
}
console.log("string_utf16_units: all " + cases.length + " cases passed");
`;
fs.writeFileSync(process.argv[3], out);
