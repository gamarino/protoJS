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


// Copyright (C) 2017 Josh Wolfe. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: String.prototype.indexOf type coercion for searchString parameter
esid: sec-string.prototype.indexof
info: |
  String.prototype.indexOf ( searchString [ , position ] )

  3. Let searchStr be ? ToString(searchString).
features: [Symbol.toPrimitive, computed-property-names]
---*/

assert.sameValue("__foo__".indexOf(Object("foo")), 2,
  "ToPrimitive: unbox object with internal slot");
assert.sameValue("__foo__".indexOf({
  [Symbol.toPrimitive]: function() {
    return "foo";
  }
}), 2, "ToPrimitive: @@toPrimitive");
assert.sameValue("__foo__".indexOf({
  valueOf: function() {
    return "foo";
  },
  toString: null
}), 2, "ToPrimitive: valueOf");
assert.sameValue("__foo__".indexOf({
  toString: function() {
    return "foo";
  }
}), 2, "ToPrimitive: toString");
assert.sameValue("__undefined__".indexOf({
  [Symbol.toPrimitive]: function() {
    return undefined;
  }
}), 2, 'ToString: @@toPrimitive => undefined => "undefined"');
assert.sameValue("__undefined__".indexOf({
  valueOf: function() {
    return undefined;
  },
  toString: null
}), 2, 'ToString: valueOf => undefined => "undefined"');
assert.sameValue("__undefined__".indexOf({
  toString: function() {
    return undefined;
  }
}), 2, 'ToString: toString => undefined => "undefined"');
assert.sameValue("__null__".indexOf({
  [Symbol.toPrimitive]: function() {
    return null;
  }
}), 2, 'ToString: @@toPrimitive => null => "null"');
assert.sameValue("__null__".indexOf({
  valueOf: function() {
    return null;
  },
  toString: null
}), 2, 'ToString: valueOf => null => "null"');
assert.sameValue("__null__".indexOf({
  toString: function() {
    return null;
  }
}), 2, 'ToString: toString => null => "null"');
assert.sameValue("__false__".indexOf(Object(false)), 2,
  'ToString: unbox object with internal slot => false => "false"');
assert.sameValue("__false__".indexOf({
  [Symbol.toPrimitive]: function() {
    return false;
  }
}), 2, 'ToString: @@toPrimitive => false => "false"');
assert.sameValue("__false__".indexOf({
  valueOf: function() {
    return false;
  },
  toString: null
}), 2, 'ToString: valueOf => false => "false"');
assert.sameValue("__false__".indexOf({
  toString: function() {
    return false;
  }
}), 2, 'ToString: toString => false => "false"');
assert.sameValue("__0__".indexOf(Object(0)), 2,
  "ToString: unbox object with internal slot => Number to String");
assert.sameValue("__0__".indexOf({
  [Symbol.toPrimitive]: function() {
    return 0;
  }
}), 2, "ToString: @@toPrimitive => Number to String");
assert.sameValue("__0__".indexOf({
  valueOf: function() {
    return 0;
  },
  toString: null
}), 2, "ToString: valueOf => Number to String");
assert.sameValue("__0__".indexOf({
  toString: function() {
    return 0;
  }
}), 2, "ToString: toString => Number to String");
