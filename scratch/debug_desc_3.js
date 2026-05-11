var proto = { value: "inherited" };
var child = Object.create(proto);
Object.defineProperty(child, "value", {
  get: function() { return "own"; }
});
console.log("child.hasOwnProperty('value'):", child.hasOwnProperty("value"));
console.log("child.value:", child.value);
