// Test: dgram.Socket.addMembership on udp6 socket joins IPv6 multicast group.
// Requires a loopback interface supporting multicast (standard Linux/macOS).
console.log("=== dgram IPv6 addMembership ===");

const s = dgram.createSocket('udp6');

// Before fix this throws "not implemented"; after fix it must NOT throw.
let threw = false;
try {
    const result = s.addMembership('ff02::1');   // all-nodes link-local multicast
    console.log("addMembership returned:", result);
} catch (e) {
    threw = true;
    console.log("FAIL - threw:", e.message);
}

if (!threw) {
    console.log("PASS - addMembership did not throw");
} else {
    process.exit(1);
}

s.close();
console.log("=== done ===");
