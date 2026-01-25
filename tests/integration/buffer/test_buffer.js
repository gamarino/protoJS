// Test Buffer module

console.log("=== Buffer Module Tests ===\n");

// Test 1: Buffer.from(string)
try {
    const buf1 = Buffer.from("Hello");
    console.log("✅ Test 1: Buffer.from(string) - PASS");
    console.log("   Length:", buf1.length);
    console.log("   toString():", buf1.toString());
} catch (e) {
    console.log("❌ Test 1: Buffer.from(string) - FAIL:", e);
}

// Test 2: Buffer.alloc(size)
try {
    const buf2 = Buffer.alloc(10);
    console.log("✅ Test 2: Buffer.alloc(size) - PASS");
    console.log("   Length:", buf2.length);
} catch (e) {
    console.log("❌ Test 2: Buffer.alloc(size) - FAIL:", e);
}

// Test 3: Buffer.from(array)
try {
    const buf3 = Buffer.from([1, 2, 3, 4, 5]);
    console.log("✅ Test 3: Buffer.from(array) - PASS");
    console.log("   Length:", buf3.length);
} catch (e) {
    console.log("❌ Test 3: Buffer.from(array) - FAIL:", e);
}

// Test 4: Buffer.isBuffer
try {
    const buf4 = Buffer.alloc(5);
    const isBuf = Buffer.isBuffer(buf4);
    console.log("✅ Test 4: Buffer.isBuffer - PASS");
    console.log("   isBuffer:", isBuf);
} catch (e) {
    console.log("❌ Test 4: Buffer.isBuffer - FAIL:", e);
}

// Test 5: buffer.toString()
try {
    const buf5 = Buffer.from("test");
    const str = buf5.toString();
    console.log("✅ Test 5: buffer.toString() - PASS");
    console.log("   Result:", str);
} catch (e) {
    console.log("❌ Test 5: buffer.toString() - FAIL:", e);
}

// Test 6: buffer.slice()
try {
    const buf6 = Buffer.from("Hello World");
    const slice = buf6.slice(0, 5);
    console.log("✅ Test 6: buffer.slice() - PASS");
    console.log("   Slice:", slice.toString());
} catch (e) {
    console.log("❌ Test 6: buffer.slice() - FAIL:", e);
}

// Test 7: buffer.copy()
try {
    const src = Buffer.from("Hello");
    const dst = Buffer.alloc(10);
    src.copy(dst);
    console.log("✅ Test 7: buffer.copy() - PASS");
    console.log("   Copied:", dst.toString());
} catch (e) {
    console.log("❌ Test 7: buffer.copy() - FAIL:", e);
}

// Test 8: buffer.fill()
try {
    const buf8 = Buffer.alloc(5);
    buf8.fill(65); // 'A'
    console.log("✅ Test 8: buffer.fill() - PASS");
    console.log("   Filled:", buf8.toString());
} catch (e) {
    console.log("❌ Test 8: buffer.fill() - FAIL:", e);
}

console.log("\n=== Buffer Module Tests Complete ===");
