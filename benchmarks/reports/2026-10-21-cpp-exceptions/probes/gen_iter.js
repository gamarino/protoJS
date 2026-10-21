function* g(n) { for (let i = 0; i < n; i++) yield i; }
function run(n) { let c = 0; for (let k = 0; k < n; k++) for (const v of g(3)) c += v; return c; }
console.log("CHECK", run(200000));
