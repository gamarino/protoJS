var proto = {};
Object.defineProperty(proto, "value", {
  get: function() { return "inherited"; }
});
var child = Object.create(proto);
Object.defineProperty(child, "value", { value: "own" });
console.log("child.value:", child.value);
var obj = {};
Object.defineProperty(obj, "prop", child);
console.log("obj.prop:", obj.prop);
