console.log("=== require twice ===");
const a = require("./pkg/dummy_module");
const b = require("./pkg/dummy_module");
console.log("same object:", a === b);
