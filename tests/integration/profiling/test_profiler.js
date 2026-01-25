// Test Profiler module

console.log("=== Profiler Module Tests ===\n");

// Test 1: profiler.startProfiling
try {
    if (typeof profiler !== 'undefined' && typeof profiler.startProfiling === 'function') {
        const started = profiler.startProfiling();
        console.log("✅ Test 1: profiler.startProfiling - PASS");
        console.log("   Started:", started);
    } else {
        console.log("❌ Test 1: profiler.startProfiling - FAIL: not available");
    }
} catch (e) {
    console.log("❌ Test 1: profiler.startProfiling - FAIL:", e);
}

// Test 2: profiler.stopProfiling
try {
    if (typeof profiler !== 'undefined' && typeof profiler.stopProfiling === 'function') {
        const duration = profiler.stopProfiling();
        console.log("✅ Test 2: profiler.stopProfiling - PASS");
        console.log("   Duration:", duration, "ms");
    } else {
        console.log("❌ Test 2: profiler.stopProfiling - FAIL: not available");
    }
} catch (e) {
    console.log("❌ Test 2: profiler.stopProfiling - FAIL:", e);
}

// Test 3: profiler.getProfile
try {
    if (typeof profiler !== 'undefined' && typeof profiler.getProfile === 'function') {
        const profile = profiler.getProfile();
        console.log("✅ Test 3: profiler.getProfile - PASS");
        console.log("   Profile:", typeof profile);
    } else {
        console.log("❌ Test 3: profiler.getProfile - FAIL: not available");
    }
} catch (e) {
    console.log("❌ Test 3: profiler.getProfile - FAIL:", e);
}

console.log("\n=== Profiler Module Tests Complete ===");
