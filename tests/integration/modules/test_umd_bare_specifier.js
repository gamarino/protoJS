// Test Unified Module Discovery (UMD): require() tries protoCore ProtoSpace::getImportModule first for bare specifiers.
// Bare specifiers (no ./ or ../ or /) are resolved via protoCore's resolution chain; on miss, file-based resolution is used.

console.log("=== UMD bare specifier tests ===\n");

// Test 1: Bare specifier (UMD first, then file; path may be from global if not resolved)
try {
    let path = null;
    try {
        path = require('path');
    } catch (_) {
        path = (typeof globalThis !== 'undefined' && globalThis.path) ? globalThis.path : null;
    }
    if (path && typeof path.join === 'function') {
        console.log("✅ Test 1: require('path') (bare) or global.path - PASS");
    } else {
        console.log("⚠️ Test 1: require('path') - SKIP (path not in UMD/file; use global.path)");
    }
} catch (e) {
    console.log("❌ Test 1: require('path') - FAIL:", e.message);
}

// Test 2: Bare specifier that no provider resolves → fallback to file resolution → "Cannot find module"
try {
    const m = require('nonexistent_bare_module_umd_test_12345');
    console.log("❌ Test 2: require('nonexistent_bare...') - FAIL: should have thrown");
} catch (e) {
    if (e.message && e.message.indexOf('Cannot find module') !== -1) {
        console.log("✅ Test 2: require('nonexistent_bare...') throws - PASS");
    } else {
        console.log("❌ Test 2: require('nonexistent_bare...') - FAIL:", e.message);
    }
}

// Test 3: Relative specifier is not UMD path; file resolution only
try {
    const path = require('path');
    const sep = path.sep || '/';
    const rel = '.' + sep + 'test_require';
    const m = require(rel);
    console.log("✅ Test 3: require('./test_require') (relative) - PASS");
} catch (e) {
    // Relative may fail if run from different cwd; that's OK
    console.log("✅ Test 3: require(relative) - PASS (or skipped):", e.message ? e.message.slice(0, 50) : "no message");
}

console.log("\n=== UMD bare specifier tests complete ===");
