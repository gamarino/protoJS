function safeDiv(a, b) {
  try {
    if (b === 0) throw new Error("division by zero");
    return a / b;
  } catch (e) {
    return e.message;
  } finally {
    console.log("done");
  }
}
console.log(safeDiv(6, 3), safeDiv(1, 0));
