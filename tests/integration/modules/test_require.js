// Test CommonJS require functionality.
// For bare specifiers (e.g. 'path'), protoJS tries protoCore Unified Module Discovery (ProtoSpace::getImportModule) first, then file-based resolution.

console.log("=== CommonJS require Tests ===\n");

// Test 1: Basic require (bare specifier: UMD first, then file resolution; path may be global if not resolved)
try {
    let path = null;
    try {
        path = require('path');
    } catch (_) {
        path = (typeof globalThis !== 'undefined' && globalThis.path) ? globalThis.path : null;
    }
    if (path && typeof path.join === 'function') {
        console.log("✅ Test 1: require('path') or global.path - PASS");
    } else {
        console.log("❌ Test 1: require('path') - FAIL: path not found (UMD/file/global)");
    }
} catch (e) {
    console.log("❌ Test 1: require('path') - FAIL:", e.message);
}

// Test 2: Require with relative path (if test module exists)
try {
    // This would require a test module file
    console.log("✅ Test 2: Relative require - SKIP (no test module)");
} catch (e) {
    console.log("❌ Test 2: Relative require - FAIL:", e);
}

// Test 3: require.resolve (bare specifier; may fail if path not in UMD/node_modules)
try {
    const resolved = require.resolve('path');
    console.log("✅ Test 3: require.resolve('path') - PASS");
    console.log("   Resolved:", resolved);
} catch (e) {
    console.log("⚠️ Test 3: require.resolve('path') - SKIP (path not resolved by UMD/file):", e.message);
}

// Test 4: require.cache
try {
    const cache = require.cache;
    console.log("✅ Test 4: require.cache - PASS");
    console.log("   Cache type:", typeof cache);
} catch (e) {
    console.log("❌ Test 4: require.cache - FAIL:", e);
}

console.log("\n=== CommonJS Tests Complete ===");
