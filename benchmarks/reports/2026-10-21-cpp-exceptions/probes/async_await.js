async function one(i) { return i; }
async function run(n) { let c = 0; for (let i = 0; i < n; i++) c += await one(1); return c; }
run(100000).then(v => console.log("CHECK", v));
