// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Collection of assertion functions used throughout test262
defines: [assert]
---*/


function assert(mustBeTrue, message) {
  if (mustBeTrue === true) {
    return;
  }

  if (message === undefined) {
    message = 'Expected true but got ' + assert._toString(mustBeTrue);
  }
  throw new Test262Error(message);
}

assert._isSameValue = function (a, b) {
  if (a === b) {
    // Handle +/-0 vs. -/+0
    return a !== 0 || 1 / a === 1 / b;
  }

  // Handle NaN vs. NaN
  return a !== a && b !== b;
};

assert.sameValue = function (actual, expected, message) {
  try {
    if (assert._isSameValue(actual, expected)) {
      return;
    }
  } catch (error) {
    throw new Test262Error(message + ' (_isSameValue operation threw) ' + error);
    return;
  }

  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  message += 'Expected SameValue(«' + assert._toString(actual) + '», «' + assert._toString(expected) + '») to be true';

  throw new Test262Error(message);
};

assert.notSameValue = function (actual, unexpected, message) {
  if (!assert._isSameValue(actual, unexpected)) {
    return;
  }

  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  message += 'Expected SameValue(«' + assert._toString(actual) + '», «' + assert._toString(unexpected) + '») to be false';

  throw new Test262Error(message);
};

assert.throws = function (expectedErrorConstructor, func, message) {
  var expectedName, actualName;
  if (typeof func !== "function") {
    throw new Test262Error('assert.throws requires two arguments: the error constructor ' +
      'and a function to run');
    return;
  }
  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  try {
    func();
  } catch (thrown) {
    if (typeof thrown !== 'object' || thrown === null) {
      message += 'Thrown value was not an object!';
      throw new Test262Error(message);
    } else if (thrown.constructor !== expectedErrorConstructor) {
      expectedName = expectedErrorConstructor.name;
      actualName = thrown.constructor.name;
      if (expectedName === actualName) {
        message += 'Expected a ' + expectedName + ' but got a different error constructor with the same name';
      } else {
        message += 'Expected a ' + expectedName + ' but got a ' + actualName;
      }
      throw new Test262Error(message);
    }
    return;
  }

  message += 'Expected a ' + expectedErrorConstructor.name + ' to be thrown but no exception was thrown at all';
  throw new Test262Error(message);
};

function isPrimitive(value) {
  return !value || (typeof value !== 'object' && typeof value !== 'function');
}

assert.compareArray = function (actual, expected, message) {
  message = message === undefined ? '' : message;

  if (typeof message === 'symbol') {
    message = message.toString();
  }

  if (isPrimitive(actual)) {
    assert(false, `Actual argument [${actual}] shouldn't be primitive. ${message}`);
  } else if (isPrimitive(expected)) {
    assert(false, `Expected argument [${expected}] shouldn't be primitive. ${message}`);
  }
  var result = compareArray(actual, expected);
  if (result) return;

  var format = compareArray.format;
  assert(false, `Actual ${format(actual)} and expected ${format(expected)} should have the same contents. ${message}`);
};

function compareArray(a, b) {
  if (b.length !== a.length) {
    return false;
  }
  for (var i = 0; i < a.length; i++) {
    if (!assert._isSameValue(b[i], a[i])) {
      return false;
    }
  }
  return true;
}

compareArray.format = function (arrayLike) {
  return `[${Array.prototype.map.call(arrayLike, String).join(', ')}]`;
};

assert._formatIdentityFreeValue = function formatIdentityFreeValue(value) {
  switch (value === null ? 'null' : typeof value) {
    case 'string':
      return typeof JSON !== "undefined" ? JSON.stringify(value) : `"${value}"`;
    case 'bigint':
      return `${value}n`;
    case 'number':
      if (value === 0 && 1 / value === -Infinity) return '-0';
      // falls through
    case 'boolean':
    case 'undefined':
    case 'null':
      return String(value);
  }
};

assert._toString = function (value) {
  var basic = assert._formatIdentityFreeValue(value);
  if (basic) return basic;
  try {
    return String(value);
  } catch (err) {
    if (err.name === 'TypeError') {
      return Object.prototype.toString.call(value);
    }
    throw err;
  }
};


// Copyright (c) 2012 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Provides both:

    - An error class to avoid false positives when testing for thrown exceptions
    - A function to explicitly throw an exception using the Test262Error class
defines: [Test262Error, $DONOTEVALUATE]
---*/


function Test262Error(message) {
  this.message = message || "";
}

Test262Error.prototype.toString = function () {
  return "Test262Error: " + this.message;
};

Test262Error.thrower = function (message) {
  throw new Test262Error(message);
};

function $DONOTEVALUATE() {
  throw "Test262: This statement should not be evaluated.";
}


// Copyright (C) 2016 Rick Waldron. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Rick Waldron
esid: sec-unary-operators
description: Exponentiation Operator expression precedence of unary operators
info: |
  ExponentiationExpression :
    UnaryExpression
    ...

  UnaryExpression :
    ...
    `delete` UnaryExpression
    `void` UnaryExpression
    `typeof` UnaryExpression
    `+` UnaryExpression
    `-` UnaryExpression
    `~` UnaryExpression
    `!` UnaryExpression
features: [exponentiation]
---*/

assert.sameValue(-(3 ** 2), -9, "-(3 ** 2) === -9");
assert.sameValue(+(3 ** 2), 9, "+(3 ** 2) === 9");
assert.sameValue(~(3 ** 2), -10, "~(3 ** 2) === -10");
assert.sameValue(!(3 ** 2), false, "!(3 ** 2) === false");


assert.sameValue(2 ** -2, 0.25);

var o = { p: 1 };

assert.sameValue(2 ** delete o.p, 2, "delete o.p -> true -> ToNumber(true) -> 1");
assert.sameValue(2 ** void 1, NaN, "void 1 -> undefined -> ToNumber(undefined) -> NaN");
assert.sameValue(2 ** typeof 1, NaN, "typeof 1 -> 'number' -> ToNumber('number') -> NaN");

var s = "2";
var n = 2;

assert.sameValue(2 ** +s, 4, "+s -> +'2' -> 2 -> ToNumber(2) -> 2");
assert.sameValue(2 ** +n, 4, "+s -> +2 -> 2 -> ToNumber(2) -> 2");

assert.sameValue(2 ** -s, 0.25, "-s -> -'2' -> -2 -> ToNumber(-2) -> -2");
assert.sameValue(2 ** -n, 0.25, "-s -> -2 -> -2 -> ToNumber(-2) -> -2");

assert.sameValue(2 ** ~s, 0.125, "~s -> ~'2' -> -3 -> ToNumber(-3) -> -3");
assert.sameValue(2 ** ~n, 0.125, "~s -> ~2 -> -3 -> ToNumber(-3) -> -3");

assert.sameValue(2 ** !s, 1, "!s -> !'2' -> false -> ToNumber(false) -> 0");
assert.sameValue(2 ** !n, 1, "!s -> !2 -> false -> ToNumber(false) -> 0");


var capture = [];
var leftValue = { valueOf() { capture.push("leftValue"); return 3; }};
var rightValue = { valueOf() { capture.push("rightValue"); return 2; }};

(capture.push("left"), leftValue) ** +(capture.push("right"), rightValue);
//                                   ^
//                            Changes the order

// Expected per operator evaluation order: "left", "right", "rightValue", "leftValue"
assert.sameValue(capture[0], "left", "Expected the 1st element captured to be 'left'");
assert.sameValue(capture[1], "right", "Expected the 2nd element captured to be 'right'");
assert.sameValue(capture[2], "rightValue", "Expected the 3rd element captured to be 'rightValue'");
assert.sameValue(capture[3], "leftValue", "Expected the 4th element captured to be 'leftValue'");
