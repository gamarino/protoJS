class A { m(x) { return x + 1; } }
function run(n) { const a = new A(); let c = 0; for (let i = 0; i < n; i++) c = a.m(c); return c; }
console.log("CHECK", run(1000000));
