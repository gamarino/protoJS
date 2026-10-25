function total(xs) {
  return xs.reduce((acc, x) => acc + x, 0);
}
console.log(total([1, 2, 3]));
