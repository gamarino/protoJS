/*---
description: Object.defineProperty creates a new own data property
---*/

var obj = {};
Object.defineProperty(obj, "a", { value: 42, writable: false, enumerable: true, configurable: true });

assert.sameValue(obj.a, 42, "property value should be 42");
var desc = Object.getOwnPropertyDescriptor(obj, "a");
assert.sameValue(desc.value, 42, "descriptor.value should be 42");
assert.sameValue(desc.writable, false, "descriptor.writable should be false");
