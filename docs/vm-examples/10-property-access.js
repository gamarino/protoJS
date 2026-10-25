function read(o, k) {
  return o.name + o[k];
}
function write(o, k, v) {
  o.count = 1;
  o[k] = v;
  return o;
}
console.log(read({ name: "a", b: "c" }, "b"), write({}, "z", 2).z);
