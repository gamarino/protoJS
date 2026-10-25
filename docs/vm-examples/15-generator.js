function* range(n) {
  for (let i = 0; i < n; i++) yield i;
}
let s = 0;
for (const v of range(4)) s += v;
console.log(s);
