function make(x, y) {
  return { x: x, y: y, sum: x + y };
}
function Vec(x, y) {
  this.x = x;
  this.y = y;
}
console.log(make(1, 2).sum, new Vec(3, 4).y);
