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


// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: |
    The unshift function is intentionally generic.
    It does not require that its this value be an Array object
esid: sec-array.prototype.unshift
description: >
    The arguments are prepended to the start of the array, such that
    their order within the array is the same as the order in which
    they appear in  the argument list
---*/

var obj = {};
obj.unshift = Array.prototype.unshift;

obj.length = NaN;
var unshift = obj.unshift(-1);
if (unshift !== 1) {
  throw new Test262Error('#1: var obj = {}; obj.length = NaN; obj.unshift = Array.prototype.unshift; obj.unshift(-1) === 1. Actual: ' + (unshift));
}

if (obj.length !== 1) {
  throw new Test262Error('#2: var obj = {}; obj.length = NaN; obj.unshift = Array.prototype.unshift; obj.unshift(-1); obj.length === 1. Actual: ' + (obj.length));
}

if (obj["0"] !== -1) {
  throw new Test262Error('#3: var obj = {}; obj.length = NaN; obj.unshift = Array.prototype.unshift; obj.unshift(-1); obj["0"] === -1. Actual: ' + (obj["0"]));
}

obj.length = Number.NEGATIVE_INFINITY;
var unshift = obj.unshift(-7);
if (unshift !== 1) {
  throw new Test262Error('#7: var obj = {}; obj.length = Number.NEGATIVE_INFINITY; obj.unshift = Array.prototype.unshift; obj.unshift(-7) === 1. Actual: ' + (unshift));
}

if (obj.length !== 1) {
  throw new Test262Error('#8: var obj = {}; obj.length = Number.NEGATIVE_INFINITY; obj.unshift = Array.prototype.unshift; obj.unshift(-7); obj.length === 1. Actual: ' + (obj.length));
}

if (obj["0"] !== -7) {
  throw new Test262Error('#9: var obj = {}; obj.length = Number.NEGATIVE_INFINITY; obj.unshift = Array.prototype.unshift; obj.unshift(-7); obj["0"] === -7. Actual: ' + (obj["0"]));
}

obj.length = 0.5;
var unshift = obj.unshift(-10);
if (unshift !== 1) {
  throw new Test262Error('#10: var obj = {}; obj.length = 0.5; obj.unshift = Array.prototype.unshift; obj.unshift(-10) === 1. Actual: ' + (unshift));
}

if (obj.length !== 1) {
  throw new Test262Error('#11: var obj = {}; obj.length = 0.5; obj.unshift = Array.prototype.unshift; obj.unshift(-10); obj.length === 1. Actual: ' + (obj.length));
}

if (obj["0"] !== -10) {
  throw new Test262Error('#12: var obj = {}; obj.length = 0.5; obj.unshift = Array.prototype.unshift; obj.unshift(-10); obj["0"] === -10. Actual: ' + (obj["0"]));
}

obj.length = 1.5;
var unshift = obj.unshift(-13);
if (unshift !== 2) {
  throw new Test262Error('#13: var obj = {}; obj.length = 1.5; obj.unshift = Array.prototype.unshift; obj.unshift(-13) === 2. Actual: ' + (unshift));
}

if (obj.length !== 2) {
  throw new Test262Error('#14: var obj = {}; obj.length = 1.5; obj.unshift = Array.prototype.unshift; obj.unshift(-13); obj.length === 2. Actual: ' + (obj.length));
}

if (obj["0"] !== -13) {
  throw new Test262Error('#15: var obj = {}; obj.length = 1.5; obj.unshift = Array.prototype.unshift; obj.unshift(-13); obj["0"] === -13. Actual: ' + (obj["0"]));
}

obj.length = new Number(0);
var unshift = obj.unshift(-16);
if (unshift !== 1) {
  throw new Test262Error('#16: var obj = {}; obj.length = new Number(0); obj.unshift = Array.prototype.unshift; obj.unshift(-16) === 1. Actual: ' + (unshift));
}

if (obj.length !== 1) {
  throw new Test262Error('#17: var obj = {}; obj.length = new Number(0); obj.unshift = Array.prototype.unshift; obj.unshift(-16); obj.length === 1. Actual: ' + (obj.length));
}

if (obj["0"] !== -16) {
  throw new Test262Error('#18: var obj = {}; obj.length = new Number(0); obj.unshift = Array.prototype.unshift; obj.unshift(-16); obj["0"] === -16. Actual: ' + (obj["0"]));
}
