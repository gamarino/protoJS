/*---
description: let declarations are block-scoped
---*/

{
  let x = 1;
  assert.sameValue(x, 1, "x is visible inside block");
}

var threw = false;
try {
  // x should not be defined here
  /* eslint-disable no-undef */
  // @ts-ignore
  if (x === 1) {
    threw = false;
  }
} catch (e) {
  threw = true;
}

assert.sameValue(threw, true, "accessing let-declared variable outside block should throw ReferenceError");
