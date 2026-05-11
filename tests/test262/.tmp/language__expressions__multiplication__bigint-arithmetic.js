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


// Copyright (C) 2017 Robin Templeton. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-multiplicative-operators-runtime-semantics-evaluation
description: BigInt multiplication arithmetic
features: [BigInt]
---*/
assert.sameValue(
  0xFEDCBA9876543210n * 0xFEDCBA9876543210n,
  0xFDBAC097C8DC5ACCDEEC6CD7A44A4100n,
  'The result of (0xFEDCBA9876543210n * 0xFEDCBA9876543210n) is 0xFDBAC097C8DC5ACCDEEC6CD7A44A4100n'
);

assert.sameValue(
  0xFEDCBA9876543210n * 0xFEDCBA987654320Fn,
  0xFDBAC097C8DC5ACBE00FB23F2DF60EF0n,
  'The result of (0xFEDCBA9876543210n * 0xFEDCBA987654320Fn) is 0xFDBAC097C8DC5ACBE00FB23F2DF60EF0n'
);

assert.sameValue(
  0xFEDCBA9876543210n * 0xFEDCBA98n,
  0xFDBAC097530ECA86541D5980n,
  'The result of (0xFEDCBA9876543210n * 0xFEDCBA98n) is 0xFDBAC097530ECA86541D5980n'
);

assert.sameValue(
  0xFEDCBA9876543210n * 0xFEDCBA97n,
  0xFDBAC09654320FEDDDC92770n,
  'The result of (0xFEDCBA9876543210n * 0xFEDCBA97n) is 0xFDBAC09654320FEDDDC92770n'
);

assert.sameValue(
  0xFEDCBA9876543210n * 0x1234n,
  0x121F49F49F49F49F4B40n,
  'The result of (0xFEDCBA9876543210n * 0x1234n) is 0x121F49F49F49F49F4B40n'
);

assert.sameValue(
  0xFEDCBA9876543210n * 0x3n,
  0x2FC962FC962FC9630n,
  'The result of (0xFEDCBA9876543210n * 0x3n) is 0x2FC962FC962FC9630n'
);

assert.sameValue(
  0xFEDCBA9876543210n * 0x2n,
  0x1FDB97530ECA86420n,
  'The result of (0xFEDCBA9876543210n * 0x2n) is 0x1FDB97530ECA86420n'
);

assert.sameValue(
  0xFEDCBA9876543210n * 0x1n,
  0xFEDCBA9876543210n,
  'The result of (0xFEDCBA9876543210n * 0x1n) is 0xFEDCBA9876543210n'
);

assert.sameValue(
  0xFEDCBA9876543210n * 0x0n,
  0x0n,
  'The result of (0xFEDCBA9876543210n * 0x0n) is 0x0n'
);

assert.sameValue(
  0xFEDCBA9876543210n * -0x1n,
  -0xFEDCBA9876543210n,
  'The result of (0xFEDCBA9876543210n * -0x1n) is -0xFEDCBA9876543210n'
);

assert.sameValue(
  0xFEDCBA9876543210n * -0x2n,
  -0x1FDB97530ECA86420n,
  'The result of (0xFEDCBA9876543210n * -0x2n) is -0x1FDB97530ECA86420n'
);

assert.sameValue(
  0xFEDCBA9876543210n * -0x3n,
  -0x2FC962FC962FC9630n,
  'The result of (0xFEDCBA9876543210n * -0x3n) is -0x2FC962FC962FC9630n'
);

assert.sameValue(
  0xFEDCBA9876543210n * -0x1234n,
  -0x121F49F49F49F49F4B40n,
  'The result of (0xFEDCBA9876543210n * -0x1234n) is -0x121F49F49F49F49F4B40n'
);

assert.sameValue(
  0xFEDCBA9876543210n * -0xFEDCBA97n,
  -0xFDBAC09654320FEDDDC92770n,
  'The result of (0xFEDCBA9876543210n * -0xFEDCBA97n) is -0xFDBAC09654320FEDDDC92770n'
);

assert.sameValue(
  0xFEDCBA9876543210n * -0xFEDCBA98n,
  -0xFDBAC097530ECA86541D5980n,
  'The result of (0xFEDCBA9876543210n * -0xFEDCBA98n) is -0xFDBAC097530ECA86541D5980n'
);

assert.sameValue(
  0xFEDCBA9876543210n * -0xFEDCBA987654320Fn,
  -0xFDBAC097C8DC5ACBE00FB23F2DF60EF0n,
  'The result of (0xFEDCBA9876543210n * -0xFEDCBA987654320Fn) is -0xFDBAC097C8DC5ACBE00FB23F2DF60EF0n'
);

assert.sameValue(
  0xFEDCBA9876543210n * -0xFEDCBA9876543210n,
  -0xFDBAC097C8DC5ACCDEEC6CD7A44A4100n,
  'The result of (0xFEDCBA9876543210n * -0xFEDCBA9876543210n) is -0xFDBAC097C8DC5ACCDEEC6CD7A44A4100n'
);

assert.sameValue(
  0xFEDCBA987654320Fn * 0xFEDCBA987654320Fn,
  0xFDBAC097C8DC5ACAE132F7A6B7A1DCE1n,
  'The result of (0xFEDCBA987654320Fn * 0xFEDCBA987654320Fn) is 0xFDBAC097C8DC5ACAE132F7A6B7A1DCE1n'
);

assert.sameValue(
  0xFEDCBA987654320Fn * 0xFEDCBA98n,
  0xFDBAC097530ECA8555409EE8n,
  'The result of (0xFEDCBA987654320Fn * 0xFEDCBA98n) is 0xFDBAC097530ECA8555409EE8n'
);

assert.sameValue(
  0xFEDCBA987654320Fn * 0xFEDCBA97n,
  0xFDBAC09654320FECDEEC6CD9n,
  'The result of (0xFEDCBA987654320Fn * 0xFEDCBA97n) is 0xFDBAC09654320FECDEEC6CD9n'
);

assert.sameValue(
  0xFEDCBA987654320Fn * 0x1234n,
  0x121F49F49F49F49F390Cn,
  'The result of (0xFEDCBA987654320Fn * 0x1234n) is 0x121F49F49F49F49F390Cn'
);

assert.sameValue(
  0xFEDCBA987654320Fn * 0x3n,
  0x2FC962FC962FC962Dn,
  'The result of (0xFEDCBA987654320Fn * 0x3n) is 0x2FC962FC962FC962Dn'
);

assert.sameValue(
  0xFEDCBA987654320Fn * 0x2n,
  0x1FDB97530ECA8641En,
  'The result of (0xFEDCBA987654320Fn * 0x2n) is 0x1FDB97530ECA8641En'
);

assert.sameValue(
  0xFEDCBA987654320Fn * 0x1n,
  0xFEDCBA987654320Fn,
  'The result of (0xFEDCBA987654320Fn * 0x1n) is 0xFEDCBA987654320Fn'
);

assert.sameValue(
  0xFEDCBA987654320Fn * 0x0n,
  0x0n,
  'The result of (0xFEDCBA987654320Fn * 0x0n) is 0x0n'
);

assert.sameValue(
  0xFEDCBA987654320Fn * -0x1n,
  -0xFEDCBA987654320Fn,
  'The result of (0xFEDCBA987654320Fn * -0x1n) is -0xFEDCBA987654320Fn'
);

assert.sameValue(
  0xFEDCBA987654320Fn * -0x2n,
  -0x1FDB97530ECA8641En,
  'The result of (0xFEDCBA987654320Fn * -0x2n) is -0x1FDB97530ECA8641En'
);

assert.sameValue(
  0xFEDCBA987654320Fn * -0x3n,
  -0x2FC962FC962FC962Dn,
  'The result of (0xFEDCBA987654320Fn * -0x3n) is -0x2FC962FC962FC962Dn'
);

assert.sameValue(
  0xFEDCBA987654320Fn * -0x1234n,
  -0x121F49F49F49F49F390Cn,
  'The result of (0xFEDCBA987654320Fn * -0x1234n) is -0x121F49F49F49F49F390Cn'
);

assert.sameValue(
  0xFEDCBA987654320Fn * -0xFEDCBA97n,
  -0xFDBAC09654320FECDEEC6CD9n,
  'The result of (0xFEDCBA987654320Fn * -0xFEDCBA97n) is -0xFDBAC09654320FECDEEC6CD9n'
);

assert.sameValue(
  0xFEDCBA987654320Fn * -0xFEDCBA98n,
  -0xFDBAC097530ECA8555409EE8n,
  'The result of (0xFEDCBA987654320Fn * -0xFEDCBA98n) is -0xFDBAC097530ECA8555409EE8n'
);

assert.sameValue(
  0xFEDCBA987654320Fn * -0xFEDCBA987654320Fn,
  -0xFDBAC097C8DC5ACAE132F7A6B7A1DCE1n,
  'The result of (0xFEDCBA987654320Fn * -0xFEDCBA987654320Fn) is -0xFDBAC097C8DC5ACAE132F7A6B7A1DCE1n'
);

assert.sameValue(
  0xFEDCBA987654320Fn * -0xFEDCBA9876543210n,
  -0xFDBAC097C8DC5ACBE00FB23F2DF60EF0n,
  'The result of (0xFEDCBA987654320Fn * -0xFEDCBA9876543210n) is -0xFDBAC097C8DC5ACBE00FB23F2DF60EF0n'
);

assert.sameValue(
  0xFEDCBA98n * 0xFEDCBA98n,
  0xFDBAC096DD413A40n,
  'The result of (0xFEDCBA98n * 0xFEDCBA98n) is 0xFDBAC096DD413A40n'
);

assert.sameValue(
  0xFEDCBA98n * 0xFEDCBA97n,
  0xFDBAC095DE647FA8n,
  'The result of (0xFEDCBA98n * 0xFEDCBA97n) is 0xFDBAC095DE647FA8n'
);

assert.sameValue(
  0xFEDCBA98n * 0x1234n,
  0x121F49F496E0n,
  'The result of (0xFEDCBA98n * 0x1234n) is 0x121F49F496E0n'
);

assert.sameValue(
  0xFEDCBA98n * 0x3n,
  0x2FC962FC8n,
  'The result of (0xFEDCBA98n * 0x3n) is 0x2FC962FC8n'
);

assert.sameValue(
  0xFEDCBA98n * 0x2n,
  0x1FDB97530n,
  'The result of (0xFEDCBA98n * 0x2n) is 0x1FDB97530n'
);

assert.sameValue(
  0xFEDCBA98n * 0x1n,
  0xFEDCBA98n,
  'The result of (0xFEDCBA98n * 0x1n) is 0xFEDCBA98n'
);

assert.sameValue(0xFEDCBA98n * 0x0n, 0x0n, 'The result of (0xFEDCBA98n * 0x0n) is 0x0n');

assert.sameValue(
  0xFEDCBA98n * -0x1n,
  -0xFEDCBA98n,
  'The result of (0xFEDCBA98n * -0x1n) is -0xFEDCBA98n'
);

assert.sameValue(
  0xFEDCBA98n * -0x2n,
  -0x1FDB97530n,
  'The result of (0xFEDCBA98n * -0x2n) is -0x1FDB97530n'
);

assert.sameValue(
  0xFEDCBA98n * -0x3n,
  -0x2FC962FC8n,
  'The result of (0xFEDCBA98n * -0x3n) is -0x2FC962FC8n'
);

assert.sameValue(
  0xFEDCBA98n * -0x1234n,
  -0x121F49F496E0n,
  'The result of (0xFEDCBA98n * -0x1234n) is -0x121F49F496E0n'
);

assert.sameValue(
  0xFEDCBA98n * -0xFEDCBA97n,
  -0xFDBAC095DE647FA8n,
  'The result of (0xFEDCBA98n * -0xFEDCBA97n) is -0xFDBAC095DE647FA8n'
);

assert.sameValue(
  0xFEDCBA98n * -0xFEDCBA98n,
  -0xFDBAC096DD413A40n,
  'The result of (0xFEDCBA98n * -0xFEDCBA98n) is -0xFDBAC096DD413A40n'
);

assert.sameValue(
  0xFEDCBA98n * -0xFEDCBA987654320Fn,
  -0xFDBAC097530ECA8555409EE8n,
  'The result of (0xFEDCBA98n * -0xFEDCBA987654320Fn) is -0xFDBAC097530ECA8555409EE8n'
);

assert.sameValue(
  0xFEDCBA98n * -0xFEDCBA9876543210n,
  -0xFDBAC097530ECA86541D5980n,
  'The result of (0xFEDCBA98n * -0xFEDCBA9876543210n) is -0xFDBAC097530ECA86541D5980n'
);

assert.sameValue(
  0xFEDCBA97n * 0xFEDCBA97n,
  0xFDBAC094DF87C511n,
  'The result of (0xFEDCBA97n * 0xFEDCBA97n) is 0xFDBAC094DF87C511n'
);

assert.sameValue(
  0xFEDCBA97n * 0x1234n,
  0x121F49F484ACn,
  'The result of (0xFEDCBA97n * 0x1234n) is 0x121F49F484ACn'
);

assert.sameValue(
  0xFEDCBA97n * 0x3n,
  0x2FC962FC5n,
  'The result of (0xFEDCBA97n * 0x3n) is 0x2FC962FC5n'
);

assert.sameValue(
  0xFEDCBA97n * 0x2n,
  0x1FDB9752En,
  'The result of (0xFEDCBA97n * 0x2n) is 0x1FDB9752En'
);

assert.sameValue(
  0xFEDCBA97n * 0x1n,
  0xFEDCBA97n,
  'The result of (0xFEDCBA97n * 0x1n) is 0xFEDCBA97n'
);

assert.sameValue(0xFEDCBA97n * 0x0n, 0x0n, 'The result of (0xFEDCBA97n * 0x0n) is 0x0n');

assert.sameValue(
  0xFEDCBA97n * -0x1n,
  -0xFEDCBA97n,
  'The result of (0xFEDCBA97n * -0x1n) is -0xFEDCBA97n'
);

assert.sameValue(
  0xFEDCBA97n * -0x2n,
  -0x1FDB9752En,
  'The result of (0xFEDCBA97n * -0x2n) is -0x1FDB9752En'
);

assert.sameValue(
  0xFEDCBA97n * -0x3n,
  -0x2FC962FC5n,
  'The result of (0xFEDCBA97n * -0x3n) is -0x2FC962FC5n'
);

assert.sameValue(
  0xFEDCBA97n * -0x1234n,
  -0x121F49F484ACn,
  'The result of (0xFEDCBA97n * -0x1234n) is -0x121F49F484ACn'
);

assert.sameValue(
  0xFEDCBA97n * -0xFEDCBA97n,
  -0xFDBAC094DF87C511n,
  'The result of (0xFEDCBA97n * -0xFEDCBA97n) is -0xFDBAC094DF87C511n'
);

assert.sameValue(
  0xFEDCBA97n * -0xFEDCBA98n,
  -0xFDBAC095DE647FA8n,
  'The result of (0xFEDCBA97n * -0xFEDCBA98n) is -0xFDBAC095DE647FA8n'
);

assert.sameValue(
  0xFEDCBA97n * -0xFEDCBA987654320Fn,
  -0xFDBAC09654320FECDEEC6CD9n,
  'The result of (0xFEDCBA97n * -0xFEDCBA987654320Fn) is -0xFDBAC09654320FECDEEC6CD9n'
);

assert.sameValue(
  0xFEDCBA97n * -0xFEDCBA9876543210n,
  -0xFDBAC09654320FEDDDC92770n,
  'The result of (0xFEDCBA97n * -0xFEDCBA9876543210n) is -0xFDBAC09654320FEDDDC92770n'
);

assert.sameValue(0x1234n * 0x1234n, 0x14B5A90n, 'The result of (0x1234n * 0x1234n) is 0x14B5A90n');
assert.sameValue(0x1234n * 0x3n, 0x369Cn, 'The result of (0x1234n * 0x3n) is 0x369Cn');
assert.sameValue(0x1234n * 0x2n, 0x2468n, 'The result of (0x1234n * 0x2n) is 0x2468n');
assert.sameValue(0x1234n * 0x1n, 0x1234n, 'The result of (0x1234n * 0x1n) is 0x1234n');
assert.sameValue(0x1234n * 0x0n, 0x0n, 'The result of (0x1234n * 0x0n) is 0x0n');
assert.sameValue(0x1234n * -0x1n, -0x1234n, 'The result of (0x1234n * -0x1n) is -0x1234n');
assert.sameValue(0x1234n * -0x2n, -0x2468n, 'The result of (0x1234n * -0x2n) is -0x2468n');
assert.sameValue(0x1234n * -0x3n, -0x369Cn, 'The result of (0x1234n * -0x3n) is -0x369Cn');

assert.sameValue(
  0x1234n * -0x1234n,
  -0x14B5A90n,
  'The result of (0x1234n * -0x1234n) is -0x14B5A90n'
);

assert.sameValue(
  0x1234n * -0xFEDCBA97n,
  -0x121F49F484ACn,
  'The result of (0x1234n * -0xFEDCBA97n) is -0x121F49F484ACn'
);

assert.sameValue(
  0x1234n * -0xFEDCBA98n,
  -0x121F49F496E0n,
  'The result of (0x1234n * -0xFEDCBA98n) is -0x121F49F496E0n'
);

assert.sameValue(
  0x1234n * -0xFEDCBA987654320Fn,
  -0x121F49F49F49F49F390Cn,
  'The result of (0x1234n * -0xFEDCBA987654320Fn) is -0x121F49F49F49F49F390Cn'
);

assert.sameValue(
  0x1234n * -0xFEDCBA9876543210n,
  -0x121F49F49F49F49F4B40n,
  'The result of (0x1234n * -0xFEDCBA9876543210n) is -0x121F49F49F49F49F4B40n'
);

assert.sameValue(0x3n * 0x3n, 0x9n, 'The result of (0x3n * 0x3n) is 0x9n');
assert.sameValue(0x3n * 0x2n, 0x6n, 'The result of (0x3n * 0x2n) is 0x6n');
assert.sameValue(0x3n * 0x1n, 0x3n, 'The result of (0x3n * 0x1n) is 0x3n');
assert.sameValue(0x3n * 0x0n, 0x0n, 'The result of (0x3n * 0x0n) is 0x0n');
assert.sameValue(0x3n * -0x1n, -0x3n, 'The result of (0x3n * -0x1n) is -0x3n');
assert.sameValue(0x3n * -0x2n, -0x6n, 'The result of (0x3n * -0x2n) is -0x6n');
assert.sameValue(0x3n * -0x3n, -0x9n, 'The result of (0x3n * -0x3n) is -0x9n');
assert.sameValue(0x3n * -0x1234n, -0x369Cn, 'The result of (0x3n * -0x1234n) is -0x369Cn');

assert.sameValue(
  0x3n * -0xFEDCBA97n,
  -0x2FC962FC5n,
  'The result of (0x3n * -0xFEDCBA97n) is -0x2FC962FC5n'
);

assert.sameValue(
  0x3n * -0xFEDCBA98n,
  -0x2FC962FC8n,
  'The result of (0x3n * -0xFEDCBA98n) is -0x2FC962FC8n'
);

assert.sameValue(
  0x3n * -0xFEDCBA987654320Fn,
  -0x2FC962FC962FC962Dn,
  'The result of (0x3n * -0xFEDCBA987654320Fn) is -0x2FC962FC962FC962Dn'
);

assert.sameValue(
  0x3n * -0xFEDCBA9876543210n,
  -0x2FC962FC962FC9630n,
  'The result of (0x3n * -0xFEDCBA9876543210n) is -0x2FC962FC962FC9630n'
);

assert.sameValue(0x2n * 0x2n, 0x4n, 'The result of (0x2n * 0x2n) is 0x4n');
assert.sameValue(0x2n * 0x1n, 0x2n, 'The result of (0x2n * 0x1n) is 0x2n');
assert.sameValue(0x2n * 0x0n, 0x0n, 'The result of (0x2n * 0x0n) is 0x0n');
assert.sameValue(0x2n * -0x1n, -0x2n, 'The result of (0x2n * -0x1n) is -0x2n');
assert.sameValue(0x2n * -0x2n, -0x4n, 'The result of (0x2n * -0x2n) is -0x4n');
assert.sameValue(0x2n * -0x3n, -0x6n, 'The result of (0x2n * -0x3n) is -0x6n');
assert.sameValue(0x2n * -0x1234n, -0x2468n, 'The result of (0x2n * -0x1234n) is -0x2468n');

assert.sameValue(
  0x2n * -0xFEDCBA97n,
  -0x1FDB9752En,
  'The result of (0x2n * -0xFEDCBA97n) is -0x1FDB9752En'
);

assert.sameValue(
  0x2n * -0xFEDCBA98n,
  -0x1FDB97530n,
  'The result of (0x2n * -0xFEDCBA98n) is -0x1FDB97530n'
);

assert.sameValue(
  0x2n * -0xFEDCBA987654320Fn,
  -0x1FDB97530ECA8641En,
  'The result of (0x2n * -0xFEDCBA987654320Fn) is -0x1FDB97530ECA8641En'
);

assert.sameValue(
  0x2n * -0xFEDCBA9876543210n,
  -0x1FDB97530ECA86420n,
  'The result of (0x2n * -0xFEDCBA9876543210n) is -0x1FDB97530ECA86420n'
);

assert.sameValue(0x1n * 0x1n, 0x1n, 'The result of (0x1n * 0x1n) is 0x1n');
assert.sameValue(0x1n * 0x0n, 0x0n, 'The result of (0x1n * 0x0n) is 0x0n');
assert.sameValue(0x1n * -0x1n, -0x1n, 'The result of (0x1n * -0x1n) is -0x1n');
assert.sameValue(0x1n * -0x2n, -0x2n, 'The result of (0x1n * -0x2n) is -0x2n');
assert.sameValue(0x1n * -0x3n, -0x3n, 'The result of (0x1n * -0x3n) is -0x3n');
assert.sameValue(0x1n * -0x1234n, -0x1234n, 'The result of (0x1n * -0x1234n) is -0x1234n');

assert.sameValue(
  0x1n * -0xFEDCBA97n,
  -0xFEDCBA97n,
  'The result of (0x1n * -0xFEDCBA97n) is -0xFEDCBA97n'
);

assert.sameValue(
  0x1n * -0xFEDCBA98n,
  -0xFEDCBA98n,
  'The result of (0x1n * -0xFEDCBA98n) is -0xFEDCBA98n'
);

assert.sameValue(
  0x1n * -0xFEDCBA987654320Fn,
  -0xFEDCBA987654320Fn,
  'The result of (0x1n * -0xFEDCBA987654320Fn) is -0xFEDCBA987654320Fn'
);

assert.sameValue(
  0x1n * -0xFEDCBA9876543210n,
  -0xFEDCBA9876543210n,
  'The result of (0x1n * -0xFEDCBA9876543210n) is -0xFEDCBA9876543210n'
);

assert.sameValue(0x0n * 0x0n, 0x0n, 'The result of (0x0n * 0x0n) is 0x0n');
assert.sameValue(0x0n * -0x1n, 0x0n, 'The result of (0x0n * -0x1n) is 0x0n');
assert.sameValue(0x0n * -0x2n, 0x0n, 'The result of (0x0n * -0x2n) is 0x0n');
assert.sameValue(0x0n * -0x3n, 0x0n, 'The result of (0x0n * -0x3n) is 0x0n');
assert.sameValue(0x0n * -0x1234n, 0x0n, 'The result of (0x0n * -0x1234n) is 0x0n');
assert.sameValue(0x0n * -0xFEDCBA97n, 0x0n, 'The result of (0x0n * -0xFEDCBA97n) is 0x0n');
assert.sameValue(0x0n * -0xFEDCBA98n, 0x0n, 'The result of (0x0n * -0xFEDCBA98n) is 0x0n');

assert.sameValue(
  0x0n * -0xFEDCBA987654320Fn,
  0x0n,
  'The result of (0x0n * -0xFEDCBA987654320Fn) is 0x0n'
);

assert.sameValue(
  0x0n * -0xFEDCBA9876543210n,
  0x0n,
  'The result of (0x0n * -0xFEDCBA9876543210n) is 0x0n'
);

assert.sameValue(-0x1n * -0x1n, 0x1n, 'The result of (-0x1n * -0x1n) is 0x1n');
assert.sameValue(-0x1n * -0x2n, 0x2n, 'The result of (-0x1n * -0x2n) is 0x2n');
assert.sameValue(-0x1n * -0x3n, 0x3n, 'The result of (-0x1n * -0x3n) is 0x3n');
assert.sameValue(-0x1n * -0x1234n, 0x1234n, 'The result of (-0x1n * -0x1234n) is 0x1234n');

assert.sameValue(
  -0x1n * -0xFEDCBA97n,
  0xFEDCBA97n,
  'The result of (-0x1n * -0xFEDCBA97n) is 0xFEDCBA97n'
);

assert.sameValue(
  -0x1n * -0xFEDCBA98n,
  0xFEDCBA98n,
  'The result of (-0x1n * -0xFEDCBA98n) is 0xFEDCBA98n'
);

assert.sameValue(
  -0x1n * -0xFEDCBA987654320Fn,
  0xFEDCBA987654320Fn,
  'The result of (-0x1n * -0xFEDCBA987654320Fn) is 0xFEDCBA987654320Fn'
);

assert.sameValue(
  -0x1n * -0xFEDCBA9876543210n,
  0xFEDCBA9876543210n,
  'The result of (-0x1n * -0xFEDCBA9876543210n) is 0xFEDCBA9876543210n'
);

assert.sameValue(-0x2n * -0x2n, 0x4n, 'The result of (-0x2n * -0x2n) is 0x4n');
assert.sameValue(-0x2n * -0x3n, 0x6n, 'The result of (-0x2n * -0x3n) is 0x6n');
assert.sameValue(-0x2n * -0x1234n, 0x2468n, 'The result of (-0x2n * -0x1234n) is 0x2468n');

assert.sameValue(
  -0x2n * -0xFEDCBA97n,
  0x1FDB9752En,
  'The result of (-0x2n * -0xFEDCBA97n) is 0x1FDB9752En'
);

assert.sameValue(
  -0x2n * -0xFEDCBA98n,
  0x1FDB97530n,
  'The result of (-0x2n * -0xFEDCBA98n) is 0x1FDB97530n'
);

assert.sameValue(
  -0x2n * -0xFEDCBA987654320Fn,
  0x1FDB97530ECA8641En,
  'The result of (-0x2n * -0xFEDCBA987654320Fn) is 0x1FDB97530ECA8641En'
);

assert.sameValue(
  -0x2n * -0xFEDCBA9876543210n,
  0x1FDB97530ECA86420n,
  'The result of (-0x2n * -0xFEDCBA9876543210n) is 0x1FDB97530ECA86420n'
);

assert.sameValue(-0x3n * -0x3n, 0x9n, 'The result of (-0x3n * -0x3n) is 0x9n');
assert.sameValue(-0x3n * -0x1234n, 0x369Cn, 'The result of (-0x3n * -0x1234n) is 0x369Cn');

assert.sameValue(
  -0x3n * -0xFEDCBA97n,
  0x2FC962FC5n,
  'The result of (-0x3n * -0xFEDCBA97n) is 0x2FC962FC5n'
);

assert.sameValue(
  -0x3n * -0xFEDCBA98n,
  0x2FC962FC8n,
  'The result of (-0x3n * -0xFEDCBA98n) is 0x2FC962FC8n'
);

assert.sameValue(
  -0x3n * -0xFEDCBA987654320Fn,
  0x2FC962FC962FC962Dn,
  'The result of (-0x3n * -0xFEDCBA987654320Fn) is 0x2FC962FC962FC962Dn'
);

assert.sameValue(
  -0x3n * -0xFEDCBA9876543210n,
  0x2FC962FC962FC9630n,
  'The result of (-0x3n * -0xFEDCBA9876543210n) is 0x2FC962FC962FC9630n'
);

assert.sameValue(
  -0x1234n * -0x1234n,
  0x14B5A90n,
  'The result of (-0x1234n * -0x1234n) is 0x14B5A90n'
);

assert.sameValue(
  -0x1234n * -0xFEDCBA97n,
  0x121F49F484ACn,
  'The result of (-0x1234n * -0xFEDCBA97n) is 0x121F49F484ACn'
);

assert.sameValue(
  -0x1234n * -0xFEDCBA98n,
  0x121F49F496E0n,
  'The result of (-0x1234n * -0xFEDCBA98n) is 0x121F49F496E0n'
);

assert.sameValue(
  -0x1234n * -0xFEDCBA987654320Fn,
  0x121F49F49F49F49F390Cn,
  'The result of (-0x1234n * -0xFEDCBA987654320Fn) is 0x121F49F49F49F49F390Cn'
);

assert.sameValue(
  -0x1234n * -0xFEDCBA9876543210n,
  0x121F49F49F49F49F4B40n,
  'The result of (-0x1234n * -0xFEDCBA9876543210n) is 0x121F49F49F49F49F4B40n'
);

assert.sameValue(
  -0xFEDCBA97n * -0xFEDCBA97n,
  0xFDBAC094DF87C511n,
  'The result of (-0xFEDCBA97n * -0xFEDCBA97n) is 0xFDBAC094DF87C511n'
);

assert.sameValue(
  -0xFEDCBA97n * -0xFEDCBA98n,
  0xFDBAC095DE647FA8n,
  'The result of (-0xFEDCBA97n * -0xFEDCBA98n) is 0xFDBAC095DE647FA8n'
);

assert.sameValue(
  -0xFEDCBA97n * -0xFEDCBA987654320Fn,
  0xFDBAC09654320FECDEEC6CD9n,
  'The result of (-0xFEDCBA97n * -0xFEDCBA987654320Fn) is 0xFDBAC09654320FECDEEC6CD9n'
);

assert.sameValue(
  -0xFEDCBA97n * -0xFEDCBA9876543210n,
  0xFDBAC09654320FEDDDC92770n,
  'The result of (-0xFEDCBA97n * -0xFEDCBA9876543210n) is 0xFDBAC09654320FEDDDC92770n'
);

assert.sameValue(
  -0xFEDCBA98n * -0xFEDCBA98n,
  0xFDBAC096DD413A40n,
  'The result of (-0xFEDCBA98n * -0xFEDCBA98n) is 0xFDBAC096DD413A40n'
);

assert.sameValue(
  -0xFEDCBA98n * -0xFEDCBA987654320Fn,
  0xFDBAC097530ECA8555409EE8n,
  'The result of (-0xFEDCBA98n * -0xFEDCBA987654320Fn) is 0xFDBAC097530ECA8555409EE8n'
);

assert.sameValue(
  -0xFEDCBA98n * -0xFEDCBA9876543210n,
  0xFDBAC097530ECA86541D5980n,
  'The result of (-0xFEDCBA98n * -0xFEDCBA9876543210n) is 0xFDBAC097530ECA86541D5980n'
);

assert.sameValue(
  -0xFEDCBA987654320Fn * -0xFEDCBA987654320Fn,
  0xFDBAC097C8DC5ACAE132F7A6B7A1DCE1n,
  'The result of (-0xFEDCBA987654320Fn * -0xFEDCBA987654320Fn) is 0xFDBAC097C8DC5ACAE132F7A6B7A1DCE1n'
);

assert.sameValue(
  -0xFEDCBA987654320Fn * -0xFEDCBA9876543210n,
  0xFDBAC097C8DC5ACBE00FB23F2DF60EF0n,
  'The result of (-0xFEDCBA987654320Fn * -0xFEDCBA9876543210n) is 0xFDBAC097C8DC5ACBE00FB23F2DF60EF0n'
);

assert.sameValue(
  -0xFEDCBA9876543210n * -0xFEDCBA9876543210n,
  0xFDBAC097C8DC5ACCDEEC6CD7A44A4100n,
  'The result of (-0xFEDCBA9876543210n * -0xFEDCBA9876543210n) is 0xFDBAC097C8DC5ACCDEEC6CD7A44A4100n'
);
