/*---
description: basic if/else control flow
---*/

var x = 0;
if (true) {
  x = 1;
} else {
  x = 2;
}

assert.sameValue(x, 1, "if(true) branch should execute");
