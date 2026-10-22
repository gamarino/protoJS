function run(n) { let c = 0; const u = undefined; for (let i = 0; i < n; i++) { try { u.x; } catch (e) { c++; } } return c; }
console.log("CHECK", run(200000));
