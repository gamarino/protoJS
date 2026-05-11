var proto = { value: "inherited" };
var child = Object.create(proto);
Object.defineProperty(child, "value", {
  get: function() { return "own"; }
});
console.log("child.value:", child.value);
var obj = {};
Object.defineProperty(obj, "prop", child);
console.log("obj.prop:", obj.prop);
