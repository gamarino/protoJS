function classify(n) {
  if (n < 0) return "negative";
  else if (n === 0) return "zero";
  return n % 2 === 0 ? "even" : "odd";
}

function day(d) {
  switch (d) {
    case 0: return "Sun";
    case 6: return "Sat";
    default: return "weekday";
  }
}

console.log(classify(-1), classify(0), classify(7), day(6), day(3));
