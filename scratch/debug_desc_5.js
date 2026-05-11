var child = {};
Object.defineProperty(child, "value", {
  get: function() { return "own"; }
});
console.log("child.value (no inheritance):", child.value);
