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
    The reverse function is intentionally generic.
    It does not require that its this value be an Array object
esid: sec-array.prototype.reverse
description: >
    Checking this for Object object, elements are objects and
    primitives, length is string
---*/

var obj = {};
obj.length = "10";
obj.reverse = Array.prototype.reverse;

obj[0] = true;
obj[2] = Infinity;
obj[4] = undefined;
obj[5] = undefined;
obj[8] = "NaN";
obj[9] = "-1";

var reverse = obj.reverse();
if (reverse !== obj) {
  throw new Test262Error('#1: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse() === obj. Actual: ' + (reverse));
}

if (obj[0] !== "-1") {
  throw new Test262Error('#2: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj[0] === "-1". Actual: ' + (obj[0]));
}

if (obj[1] !== "NaN") {
  throw new Test262Error('#3: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj[1] === "NaN". Actual: ' + (obj[1]));
}

if (obj[2] !== undefined) {
  throw new Test262Error('#4: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj[2] === undefined. Actual: ' + (obj[2]));
}

if (obj[3] !== undefined) {
  throw new Test262Error('#5: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj[3] === undefined. Actual: ' + (obj[3]));
}

if (obj[4] !== undefined) {
  throw new Test262Error('#6: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj[4] === undefined. Actual: ' + (obj[4]));
}

if (obj[5] !== undefined) {
  throw new Test262Error('#7: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj[5] === undefined. Actual: ' + (obj[5]));
}

if (obj[6] !== undefined) {
  throw new Test262Error('#8: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj[6] === undefined. Actual: ' + (obj[6]));
}

if (obj[7] !== Infinity) {
  throw new Test262Error('#9: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj[7] === Infinity. Actual: ' + (obj[7]));
}

if (obj[8] !== undefined) {
  throw new Test262Error('#10: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj[8] === undefined. Actual: ' + (obj[8]));
}

if (obj[9] !== true) {
  throw new Test262Error('#11: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj[9] === true. Actual: ' + (obj[9]));
}

obj.length = new String("9");

var reverse = obj.reverse();
if (reverse !== obj) {
  throw new Test262Error('#1: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj.length = new String("9"); obj.reverse() === obj. Actual: ' + (reverse));
}

if (obj[0] !== undefined) {
  throw new Test262Error('#12: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj.length = new String("9"); obj.reverse(); obj[0] === undefined. Actual: ' + (obj[0]));
}

if (obj[1] !== Infinity) {
  throw new Test262Error('#13: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj.length = new String("9"); obj.reverse(); obj[1] === Infinity. Actual: ' + (obj[1]));
}

if (obj[2] !== undefined) {
  throw new Test262Error('#14: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj.length = new String("9"); obj.reverse(); obj[2] === undefined. Actual: ' + (obj[2]));
}

if (obj[3] !== undefined) {
  throw new Test262Error('#15: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj.length = new String("9"); obj.reverse(); obj[3] === undefined. Actual: ' + (obj[3]));
}

if (obj[4] !== undefined) {
  throw new Test262Error('#16: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj.length = new String("9"); obj.reverse(); obj[4] === undefined. Actual: ' + (obj[4]));
}

if (obj[5] !== undefined) {
  throw new Test262Error('#17: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj.length = new String("9"); obj.reverse(); obj[5] === undefined. Actual: ' + (obj[5]));
}

if (obj[6] !== undefined) {
  throw new Test262Error('#18: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj.length = new String("9"); obj.reverse(); obj[6] === undefined. Actual: ' + (obj[6]));
}

if (obj[7] !== "NaN") {
  throw new Test262Error('#19: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj.length = new String("9"); obj.reverse(); obj[7] === "NaN". Actual: ' + (obj[7]));
}

if (obj[8] !== "-1") {
  throw new Test262Error('#20: var obj = {}; obj.reverse = Array.prototype.reverse; obj.length = "10"; obj[0] = true; obj[2] = Infinity; obj[4] = undefined; obj[5] = undefined; obj[8] = "NaN"; obj[9] = "-1"; obj.reverse(); obj.length = new String("9"); obj.reverse(); obj[8] === "-1". Actual: ' + (obj[8]));
}
