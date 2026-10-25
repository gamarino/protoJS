function f(a, b, c) { return a + b + c; }
function g(o, xs) {
  const len = o?.items?.length ?? 0;
  return f(...xs) + len;
}
console.log(g({ items: [1, 2] }, [1, 2, 3]), g(null, [0, 0, 0]));
