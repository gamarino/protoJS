// Test Stream module
console.log("=== Stream Module Tests ===\n");
const stream = require('stream');
console.log("✅ Stream module loaded");
console.log("   Readable:", typeof stream.Readable);
console.log("   Writable:", typeof stream.Writable);
console.log("   Duplex:", typeof stream.Duplex);
console.log("   Transform:", typeof stream.Transform);
console.log("\n=== Stream Module Tests Complete ===");
