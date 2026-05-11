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


// Copyright (C) 2017 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: prod-PropertyDefinition
description: >
  computed __proto__ property key is assigned to an own property
info: |
  12.2.6 Object Initializer

  PropertyDefinition[Yield, Await] :
    PropertyName[?Yield, ?Await] : AssignmentExpression[+In, ?Yield, ?Await]

  PropertyName[Yield, Await] :
    LiteralPropertyName
    ComputedPropertyName[?Yield, ?Await]

  ComputedPropertyName[Yield, Await] :
    [ AssignmentExpression[+In, ?Yield, ?Await] ]

  B.3.1__proto__ Property Names in Object Initializers

  ...
  5. If propKey is the String value "__proto__" and if IsComputedPropertyKey(propKey)
    is false, then
    a. If Type(propValue) is either Object or Null, then
        i. Return object.[[SetPrototypeOf]](propValue).
    b. Return NormalCompletion(empty).
features: [Symbol]
---*/

var obj;
var sample = {};

obj = {
  ['__proto__']: sample
};
assert.sameValue(
  Object.getPrototypeOf(obj),
  Object.prototype,
  'does not change the object prototype (ordinary object)'
);
assert(
  obj.hasOwnProperty('__proto__'),
  'computed __proto__ property is set as an own property (ordinary object)'
);
assert.sameValue(
  obj.__proto__,
  sample,
  'value is properly defined (ordinary object)'
);

obj = {
  ['__proto__']: null
};
assert.sameValue(
  Object.getPrototypeOf(obj),
  Object.prototype,
  'does not change the object prototype (null)'
);
assert(
  obj.hasOwnProperty('__proto__'),
  'computed __proto__ property is set as an own property (null)'
);
assert.sameValue(
  obj.__proto__,
  null,
  'value is properly defined (null)'
);

obj = {
  ['__proto__']: undefined
};
assert.sameValue(
  Object.getPrototypeOf(obj),
  Object.prototype,
  'does not change the object prototype (undefined)'
);
assert(
  obj.hasOwnProperty('__proto__'),
  'computed __proto__ property is set as an own property (undefined)'
);
assert.sameValue(
  obj.__proto__,
  undefined,
  'value is properly defined (undefined)'
);

var func = function() {};
obj = {
  ['__proto__']: func
};
assert.sameValue(
  Object.getPrototypeOf(obj),
  Object.prototype,
  'does not change the object prototype (func)'
);
assert(
  obj.hasOwnProperty('__proto__'),
  'computed __proto__ property is set as an own property (func)'
);
assert.sameValue(
  obj.__proto__,
  func,
  'value is properly defined (func)'
);

var symbol = Symbol('Leo');
obj = {
  ['__proto__']: symbol
};
assert.sameValue(
  Object.getPrototypeOf(obj),
  Object.prototype,
  'does not change the object prototype (symbol)'
);
assert(
  obj.hasOwnProperty('__proto__'),
  'computed __proto__ property is set as an own property (symbol)'
);
assert.sameValue(
  obj.__proto__,
  symbol,
  'value is properly defined (symbol)'
);

obj = {
  ['__proto__']: 42
};
assert.sameValue(
  Object.getPrototypeOf(obj),
  Object.prototype,
  'does not change the object prototype (number)'
);
assert(
  obj.hasOwnProperty('__proto__'),
  'computed __proto__ property is set as an own property (number)'
);
assert.sameValue(
  obj.__proto__,
  42,
  'value is properly defined (number)'
);

obj = {
  ['__proto__']: ''
};
assert.sameValue(
  Object.getPrototypeOf(obj),
  Object.prototype,
  'does not change the object prototype (string)'
);
assert(
  obj.hasOwnProperty('__proto__'),
  'computed __proto__ property is set as an own property (string)'
);
assert.sameValue(
  obj.__proto__,
  '',
  'value is properly defined (string)'
);

obj = {
  ['__proto__']: false
};
assert.sameValue(
  Object.getPrototypeOf(obj),
  Object.prototype,
  'does not change the object prototype (boolean)'
);
assert(
  obj.hasOwnProperty('__proto__'),
  'computed __proto__ property is set as an own property (boolean)'
);
assert.sameValue(
  obj.__proto__,
  false,
  'value is properly defined (boolean)'
);
