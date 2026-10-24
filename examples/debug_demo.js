// A small program to try the protoJS debugger on (docs/DEBUGGING.md).
// Set a breakpoint on a line inside `total`, press F5 and pick protoJS.
const prices = [
  { item: "pan", price: 1.5, qty: 2 },
  { item: "leche", price: 0.9, qty: 3 },
  { item: "café", price: 4.25, qty: 1 },
];

function lineTotal(entry) {
  const subtotal = entry.price * entry.qty;
  return subtotal;
}

function total(list) {
  let sum = 0;
  for (const entry of list) {
    sum += lineTotal(entry);
  }
  return sum;
}

function makeCounter() {
  let count = 0;
  return function next() {
    count++;
    return count;
  };
}

const counter = makeCounter();
counter();
counter();
console.log("calls:", counter());
console.log("total:", total(prices));
