function sum(n) {
  let s = 0;
  for (let i = 0; i < n; i++) s += i;
  return s;
}

function countdown(n) {
  let steps = 0;
  while (n > 0) { n--; steps++; }
  return steps;
}

console.log(sum(10), countdown(3));
