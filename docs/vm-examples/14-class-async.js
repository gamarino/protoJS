class Point {
  constructor(x, y) { this.x = x; this.y = y; }
  norm1() { return Math.abs(this.x) + Math.abs(this.y); }
}

class Point3 extends Point {
  constructor(x, y, z) { super(x, y); this.z = z; }
}

async function later(v) {
  const r = await v;
  return r + 1;
}

later(new Point3(1, -2, 3).norm1()).then((v) => console.log(v));
