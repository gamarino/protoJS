console.log("start");
let sum = 0;
let obj = { a: 1 };
for (let i=0; i<10000000; i++) { sum += obj.a; }
console.log("end", sum);
