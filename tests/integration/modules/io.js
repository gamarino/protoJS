// I/O module test

console.log("=== I/O Module Tests ===");

if (typeof io !== 'undefined') {
    console.log("io module available");
    
    // Test writeFile
    if (typeof io.writeFile === 'function') {
        const testContent = "Hello from protoJS I/O module!\nThis is a test file.";
        const result = io.writeFile("test_output.txt", testContent);
        console.log("writeFile result:", result);
    }
    
    // Test readFile
    if (typeof io.readFile === 'function') {
        try {
            const content = io.readFile("test_output.txt");
            console.log("readFile result:", content);
        } catch (e) {
            console.log("readFile error (expected if file doesn't exist):", e);
        }
    }
} else {
    console.log("io module not available");
}

console.log("=== I/O Module Tests Complete ===");
