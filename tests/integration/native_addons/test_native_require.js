// Test require() of native addon (shared library).
// Run from project root: ./build/protojs tests/integration/native_addons/test_native_require.js

// Build path: assume run from project root (process.cwd())
const addonPath = process.cwd() + '/build/tests/native_addons/simple/simple';

console.log('=== Native require() test ===');
console.log('Addon path:', addonPath);

let passed = 0;
let failed = 0;

try {
    const m = require(addonPath);
    if (m.version === 1) {
        console.log('OK: addon.version === 1');
        passed++;
    } else {
        console.log('FAIL: addon.version expected 1, got', m.version);
        failed++;
    }
    if (typeof m.sum === 'function') {
        const result = m.sum(2, 3);
        if (result === 5) {
            console.log('OK: addon.sum(2, 3) === 5');
            passed++;
        } else {
            console.log('FAIL: addon.sum(2, 3) expected 5, got', result);
            failed++;
        }
    } else {
        console.log('FAIL: addon.sum is not a function');
        failed++;
    }
} catch (e) {
    console.log('FAIL: require(addon) threw:', e.message);
    failed++;
}

console.log('');
console.log(passed + ' passed, ' + failed + ' failed');
process.exit(failed > 0 ? 1 : 0);
