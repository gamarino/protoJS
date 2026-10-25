const counter = { hits: 0 };
function hit() {
  counter.hits++;
  return counter.hits;
}
hit();
console.log(hit());
