var obj = {};
Function.prototype.value = "Function";
var funObj = function() {};
console.log("funObj.value:", funObj.value);
Object.defineProperty(obj, "prop", funObj);
console.log("obj.prop:", obj.prop);
