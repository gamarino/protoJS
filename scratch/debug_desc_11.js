var p = Function.prototype;
for (var i = 0; i < 5; i++) {
  console.log(i, p === Function.prototype ? "Function.prototype" : (p === Object.prototype ? "Object.prototype" : "Other"));
  p = Object.getPrototypeOf(p);
  if (!p) { console.log(i+1, "null"); break; }
}
