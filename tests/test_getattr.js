let obj = {};
obj.__getattr__ = function(key) { console.log("getattr called for", key); return 42; };
console.log(obj.foo);
