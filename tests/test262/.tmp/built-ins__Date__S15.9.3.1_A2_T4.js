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
    The [[Prototype]] property of the newly constructed object
    is set to the original Date prototype object, the one that is the
    initial value of Date.prototype
esid: sec-date-year-month-date-hours-minutes-seconds-ms
description: 5 arguments, (year, month, date, hours, minutes)
---*/

var x11 = new Date(1899, 11, 31, 23, 59);

assert.sameValue(
  typeof x11.constructor.prototype,
  "object",
  'The value of `typeof x11.constructor.prototype` is expected to be "object"'
);

var x12 = new Date(1899, 11, 31, 23, 59);
assert(Date.prototype.isPrototypeOf(x12), 'Date.prototype.isPrototypeOf(x12) must return true');

var x13 = new Date(1899, 11, 31, 23, 59);

assert.sameValue(
  Date.prototype,
  x13.constructor.prototype,
  'The value of Date.prototype is expected to equal the value of x13.constructor.prototype'
);

var x21 = new Date(1899, 12, 1, 0, 0);

assert.sameValue(
  typeof x21.constructor.prototype,
  "object",
  'The value of `typeof x21.constructor.prototype` is expected to be "object"'
);

var x22 = new Date(1899, 12, 1, 0, 0);
assert(Date.prototype.isPrototypeOf(x22), 'Date.prototype.isPrototypeOf(x22) must return true');

var x23 = new Date(1899, 12, 1, 0, 0);

assert.sameValue(
  Date.prototype,
  x23.constructor.prototype,
  'The value of Date.prototype is expected to equal the value of x23.constructor.prototype'
);

var x31 = new Date(1900, 0, 1, 0, 0);

assert.sameValue(
  typeof x31.constructor.prototype,
  "object",
  'The value of `typeof x31.constructor.prototype` is expected to be "object"'
);

var x32 = new Date(1900, 0, 1, 0, 0);
assert(Date.prototype.isPrototypeOf(x32), 'Date.prototype.isPrototypeOf(x32) must return true');

var x33 = new Date(1900, 0, 1, 0, 0);

assert.sameValue(
  Date.prototype,
  x33.constructor.prototype,
  'The value of Date.prototype is expected to equal the value of x33.constructor.prototype'
);

var x41 = new Date(1969, 11, 31, 23, 59);

assert.sameValue(
  typeof x41.constructor.prototype,
  "object",
  'The value of `typeof x41.constructor.prototype` is expected to be "object"'
);

var x42 = new Date(1969, 11, 31, 23, 59);
assert(Date.prototype.isPrototypeOf(x42), 'Date.prototype.isPrototypeOf(x42) must return true');

var x43 = new Date(1969, 11, 31, 23, 59);

assert.sameValue(
  Date.prototype,
  x43.constructor.prototype,
  'The value of Date.prototype is expected to equal the value of x43.constructor.prototype'
);

var x51 = new Date(1969, 12, 1, 0, 0);

assert.sameValue(
  typeof x51.constructor.prototype,
  "object",
  'The value of `typeof x51.constructor.prototype` is expected to be "object"'
);

var x52 = new Date(1969, 12, 1, 0, 0);
assert(Date.prototype.isPrototypeOf(x52), 'Date.prototype.isPrototypeOf(x52) must return true');

var x53 = new Date(1969, 12, 1, 0, 0);

assert.sameValue(
  Date.prototype,
  x53.constructor.prototype,
  'The value of Date.prototype is expected to equal the value of x53.constructor.prototype'
);

var x61 = new Date(1970, 0, 1, 0, 0);

assert.sameValue(
  typeof x61.constructor.prototype,
  "object",
  'The value of `typeof x61.constructor.prototype` is expected to be "object"'
);

var x62 = new Date(1970, 0, 1, 0, 0);
assert(Date.prototype.isPrototypeOf(x62), 'Date.prototype.isPrototypeOf(x62) must return true');

var x63 = new Date(1970, 0, 1, 0, 0);

assert.sameValue(
  Date.prototype,
  x63.constructor.prototype,
  'The value of Date.prototype is expected to equal the value of x63.constructor.prototype'
);

var x71 = new Date(1999, 11, 31, 23, 59);

assert.sameValue(
  typeof x71.constructor.prototype,
  "object",
  'The value of `typeof x71.constructor.prototype` is expected to be "object"'
);

var x72 = new Date(1999, 11, 31, 23, 59);
assert(Date.prototype.isPrototypeOf(x72), 'Date.prototype.isPrototypeOf(x72) must return true');

var x73 = new Date(1999, 11, 31, 23, 59);

assert.sameValue(
  Date.prototype,
  x73.constructor.prototype,
  'The value of Date.prototype is expected to equal the value of x73.constructor.prototype'
);

var x81 = new Date(1999, 12, 1, 0, 0);

assert.sameValue(
  typeof x81.constructor.prototype,
  "object",
  'The value of `typeof x81.constructor.prototype` is expected to be "object"'
);

var x82 = new Date(1999, 12, 1, 0, 0);
assert(Date.prototype.isPrototypeOf(x82), 'Date.prototype.isPrototypeOf(x82) must return true');

var x83 = new Date(1999, 12, 1, 0, 0);

assert.sameValue(
  Date.prototype,
  x83.constructor.prototype,
  'The value of Date.prototype is expected to equal the value of x83.constructor.prototype'
);

var x91 = new Date(2000, 0, 1, 0, 0);

assert.sameValue(
  typeof x91.constructor.prototype,
  "object",
  'The value of `typeof x91.constructor.prototype` is expected to be "object"'
);

var x92 = new Date(2000, 0, 1, 0, 0);
assert(Date.prototype.isPrototypeOf(x92), 'Date.prototype.isPrototypeOf(x92) must return true');

var x93 = new Date(2000, 0, 1, 0, 0);

assert.sameValue(
  Date.prototype,
  x93.constructor.prototype,
  'The value of Date.prototype is expected to equal the value of x93.constructor.prototype'
);

var x101 = new Date(2099, 11, 31, 23, 59);

assert.sameValue(
  typeof x101.constructor.prototype,
  "object",
  'The value of `typeof x101.constructor.prototype` is expected to be "object"'
);

var x102 = new Date(2099, 11, 31, 23, 59);
assert(Date.prototype.isPrototypeOf(x102), 'Date.prototype.isPrototypeOf(x102) must return true');

var x103 = new Date(2099, 11, 31, 23, 59);

assert.sameValue(
  Date.prototype,
  x103.constructor.prototype,
  'The value of Date.prototype is expected to equal the value of x103.constructor.prototype'
);

var x111 = new Date(2099, 12, 1, 0, 0);

assert.sameValue(
  typeof x111.constructor.prototype,
  "object",
  'The value of `typeof x111.constructor.prototype` is expected to be "object"'
);

var x112 = new Date(2099, 12, 1, 0, 0);
assert(Date.prototype.isPrototypeOf(x112), 'Date.prototype.isPrototypeOf(x112) must return true');

var x113 = new Date(2099, 12, 1, 0, 0);

assert.sameValue(
  Date.prototype,
  x113.constructor.prototype,
  'The value of Date.prototype is expected to equal the value of x113.constructor.prototype'
);

var x121 = new Date(2100, 0, 1, 0, 0);

assert.sameValue(
  typeof x121.constructor.prototype,
  "object",
  'The value of `typeof x121.constructor.prototype` is expected to be "object"'
);

var x122 = new Date(2100, 0, 1, 0, 0);
assert(Date.prototype.isPrototypeOf(x122), 'Date.prototype.isPrototypeOf(x122) must return true');

var x123 = new Date(2100, 0, 1, 0, 0);

assert.sameValue(
  Date.prototype,
  x123.constructor.prototype,
  'The value of Date.prototype is expected to equal the value of x123.constructor.prototype'
);
