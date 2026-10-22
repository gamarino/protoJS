function run(n) { let c = 0; for (let i = 0; i < n; i++) { try { c++; } finally { c++; } } return c; }
console.log("CHECK", run(2000000));
