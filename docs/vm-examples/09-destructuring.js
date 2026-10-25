function demo(pair, opts) {
  const [a, b] = pair;
  const { x, y = 10 } = opts;
  return a + b + x + y;
}
console.log(demo([1, 2], { x: 3 }));
