// Test Net module

console.log("=== Net Module Tests ===\n");

// Test 1: net.createServer
try {
    if (typeof net === 'undefined') {
        console.log("❌ Test 1: net module not available");
    } else {
        const server = net.createServer((socket) => {
            console.log("✅ Test 1: net.createServer - PASS");
            console.log("   Server created, connection listener set");
        });
        console.log("✅ Test 1: net.createServer - PASS");
    }
} catch (e) {
    console.log("❌ Test 1: net.createServer - FAIL:", e);
}

// Test 2: server.listen
try {
    const server = net.createServer();
    server.listen(0, () => {
        console.log("✅ Test 2: server.listen - PASS");
        const addr = server.address();
        console.log("   Listening on port:", addr.port);
        server.close();
    });
    // Give it a moment
    setTimeout(() => {}, 100);
} catch (e) {
    console.log("❌ Test 2: server.listen - FAIL:", e);
}

// Test 3: net.createConnection
try {
    if (typeof net.createConnection === 'function') {
        const socket = net.createConnection({port: 80, host: 'localhost'});
        socket.on('error', () => {
            // Expected to fail, but socket creation should work
        });
        console.log("✅ Test 3: net.createConnection - PASS");
        console.log("   Socket created");
        setTimeout(() => socket.destroy(), 100);
    } else {
        console.log("❌ Test 3: net.createConnection - FAIL: not a function");
    }
} catch (e) {
    console.log("❌ Test 3: net.createConnection - FAIL:", e);
}

// Test 4: socket.write
try {
    if (typeof net.createConnection === 'function') {
        const socket = net.createConnection({port: 80, host: 'localhost'});
        socket.on('error', () => {});
        if (typeof socket.write === 'function') {
            socket.write("test");
            console.log("✅ Test 4: socket.write - PASS");
        } else {
            console.log("❌ Test 4: socket.write - FAIL: write not a function");
        }
        setTimeout(() => socket.destroy(), 100);
    } else {
        console.log("❌ Test 4: socket.write - FAIL: createConnection not available");
    }
} catch (e) {
    console.log("❌ Test 4: socket.write - FAIL:", e);
}

// Test 5: socket.address
try {
    const server = net.createServer();
    server.listen(0, () => {
        const addr = server.address();
        if (addr && addr.port) {
            console.log("✅ Test 5: socket.address - PASS");
            console.log("   Address:", addr.address, "Port:", addr.port);
        }
        server.close();
    });
    setTimeout(() => {}, 100);
} catch (e) {
    console.log("❌ Test 5: socket.address - FAIL:", e);
}

console.log("\n=== Net Module Tests Complete ===");
