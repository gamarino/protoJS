/*---
description: prototype chain lookups should observe inherited properties
---*/

var proto = { a: 1 };
var obj = Object.create(proto);

assert.sameValue(obj.a, 1, "obj should see property a from prototype");
obj.a = 2;
assert.sameValue(obj.a, 2, "own property should shadow prototype");
assert.sameValue(proto.a, 1, "prototype remains unchanged");
