var sym = Symbol();
var obj = {};
Object.defineProperty(obj, sym, { value: 1, configurable: true });
console.log(Object.getOwnPropertySymbols(obj));
