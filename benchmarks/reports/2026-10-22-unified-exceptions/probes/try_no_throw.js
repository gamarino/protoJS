function run(n) { let c = 0; for (let i = 0; i < n; i++) { try { c++; } catch (e) { c--; } } return c; }
console.log("CHECK", run(2000000));
