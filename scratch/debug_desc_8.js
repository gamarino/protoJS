console.log("Setting Function.prototype.value");
Function.prototype.value = "Function";
console.log("Creating funObj");
var funObj = function() {};
console.log("Accessing funObj.value");
var v = funObj.value;
console.log("Result:", v);
