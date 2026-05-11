var proto = { value: "inherited" };
var child = Object.create(proto);
Object.defineProperty(child, "value", {
  get: function() { return "own"; }
});
console.log("child.hasOwnProperty('value'):", child.hasOwnProperty("value"));
console.log("child.hasOwnProperty('__get_value__'):", child.hasOwnProperty("__get_value__"));
console.log("child.value:", child.value);
