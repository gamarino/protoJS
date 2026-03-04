/*---
description: closures capture lexical environment values
---*/

function makeAdder(x) {
  return function (y) {
    return x + y;
  };
}

var add2 = makeAdder(2);
var add5 = makeAdder(5);

assert.sameValue(add2(3), 5, "closure should capture x = 2");
assert.sameValue(add5(4), 9, "closure should capture x = 5");
