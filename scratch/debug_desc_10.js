var p = Function.prototype;
var chain = [];
while (p) {
  chain.push(p);
  p = Object.getPrototypeOf(p);
  if (chain.length > 10) break;
}
console.log("Chain length:", chain.length);
