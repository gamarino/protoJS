/*---
description: unary negation must produce correct numeric result
---*/

assert.sameValue(-(-5), 5, "double negation should return original positive value");
