function run(n) { let c = 0; for (let i = 0; i < n; i++) { try { throw new Error("x"); } catch (e) { c++; } } return c; }
console.log("CHECK", run(200000));
