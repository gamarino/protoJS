function f(d) { if (d === 0) throw new Error("deep"); return f(d - 1); }
function run(n) { let c = 0; for (let i = 0; i < n; i++) { try { f(5); } catch (e) { c++; } } return c; }
console.log("CHECK", run(100000));
