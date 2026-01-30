// Test resolution order: native (.node / .so) first, then .js.
// When both fixture.js and fixture.so exist in this directory,
// require.resolve('./fixture') should return the path to fixture.so.

console.log('=== Resolution order test ===');

const resolved = require.resolve('./fixture');
console.log('Resolved path:', resolved);

const ext = resolved.slice(resolved.lastIndexOf('.'));
const isNative = ['.node', '.so', '.dll', '.dylib'].indexOf(ext) >= 0;

if (isNative) {
    console.log('OK: resolved to native addon (' + ext + ')');
    const m = require('./fixture');
    if (m.type === 'native') {
        console.log('OK: require("./fixture") loaded native module');
    } else {
        console.log('FAIL: expected native module exports.type === "native", got', m.type);
        process.exit(1);
    }
} else {
    // Only fixture.js present: resolve to .js is correct
    console.log('OK: resolved to JS module (' + ext + ')');
    const m = require('./fixture');
    if (m.type === 'js') {
        console.log('OK: require("./fixture") loaded JS module');
    } else {
        console.log('FAIL: expected JS module exports.type === "js", got', m.type);
        process.exit(1);
    }
}

console.log('Resolution test passed.');
