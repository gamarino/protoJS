// Test File System module
console.log("=== FS Module Tests ===\n");
const fs = require('fs');
console.log("✅ FS module loaded");
console.log("   readFileSync:", typeof fs.readFileSync);
console.log("   writeFileSync:", typeof fs.writeFileSync);
console.log("   readdirSync:", typeof fs.readdirSync);
console.log("   statSync:", typeof fs.statSync);
console.log("\n=== FS Module Tests Complete ===");
