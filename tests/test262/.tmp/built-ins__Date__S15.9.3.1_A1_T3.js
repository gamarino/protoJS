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
    When Date is called as part of a new expression it is
    a constructor: it initializes the newly created object
esid: sec-date-year-month-date-hours-minutes-seconds-ms
description: 4 arguments, (year, month, date, hours)
---*/
assert.sameValue(
  typeof new Date(1899, 11, 31, 23),
  "object",
  'The value of `typeof new Date(1899, 11, 31, 23)` is expected to be "object"'
);

assert.notSameValue(
  new Date(1899, 11, 31, 23),
  undefined,
  'new Date(1899, 11, 31, 23) is expected to not equal ``undefined``'
);

var x13 = new Date(1899, 11, 31, 23);
assert.sameValue(typeof x13, "object", 'The value of `typeof x13` is expected to be "object"');

var x14 = new Date(1899, 11, 31, 23);
assert.notSameValue(x14, undefined, 'The value of x14 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(1899, 12, 1, 0),
  "object",
  'The value of `typeof new Date(1899, 12, 1, 0)` is expected to be "object"'
);

assert.notSameValue(
  new Date(1899, 12, 1, 0),
  undefined,
  'new Date(1899, 12, 1, 0) is expected to not equal ``undefined``'
);

var x23 = new Date(1899, 12, 1, 0);
assert.sameValue(typeof x23, "object", 'The value of `typeof x23` is expected to be "object"');

var x24 = new Date(1899, 12, 1, 0);
assert.notSameValue(x24, undefined, 'The value of x24 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(1900, 0, 1, 0),
  "object",
  'The value of `typeof new Date(1900, 0, 1, 0)` is expected to be "object"'
);

assert.notSameValue(new Date(1900, 0, 1, 0), undefined, 'new Date(1900, 0, 1, 0) is expected to not equal ``undefined``');

var x33 = new Date(1900, 0, 1, 0);
assert.sameValue(typeof x33, "object", 'The value of `typeof x33` is expected to be "object"');

var x34 = new Date(1900, 0, 1, 0);
assert.notSameValue(x34, undefined, 'The value of x34 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(1969, 11, 31, 23),
  "object",
  'The value of `typeof new Date(1969, 11, 31, 23)` is expected to be "object"'
);

assert.notSameValue(
  new Date(1969, 11, 31, 23),
  undefined,
  'new Date(1969, 11, 31, 23) is expected to not equal ``undefined``'
);

var x43 = new Date(1969, 11, 31, 23);
assert.sameValue(typeof x43, "object", 'The value of `typeof x43` is expected to be "object"');

var x44 = new Date(1969, 11, 31, 23);
assert.notSameValue(x44, undefined, 'The value of x44 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(1969, 12, 1, 0),
  "object",
  'The value of `typeof new Date(1969, 12, 1, 0)` is expected to be "object"'
);

assert.notSameValue(
  new Date(1969, 12, 1, 0),
  undefined,
  'new Date(1969, 12, 1, 0) is expected to not equal ``undefined``'
);

var x53 = new Date(1969, 12, 1, 0);
assert.sameValue(typeof x53, "object", 'The value of `typeof x53` is expected to be "object"');

var x54 = new Date(1969, 12, 1, 0);
assert.notSameValue(x54, undefined, 'The value of x54 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(1970, 0, 1, 0),
  "object",
  'The value of `typeof new Date(1970, 0, 1, 0)` is expected to be "object"'
);

assert.notSameValue(new Date(1970, 0, 1, 0), undefined, 'new Date(1970, 0, 1, 0) is expected to not equal ``undefined``');

var x63 = new Date(1970, 0, 1, 0);
assert.sameValue(typeof x63, "object", 'The value of `typeof x63` is expected to be "object"');

var x64 = new Date(1970, 0, 1, 0);
assert.notSameValue(x64, undefined, 'The value of x64 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(1999, 11, 31, 23),
  "object",
  'The value of `typeof new Date(1999, 11, 31, 23)` is expected to be "object"'
);

assert.notSameValue(
  new Date(1999, 11, 31, 23),
  undefined,
  'new Date(1999, 11, 31, 23) is expected to not equal ``undefined``'
);

var x73 = new Date(1999, 11, 31, 23);
assert.sameValue(typeof x73, "object", 'The value of `typeof x73` is expected to be "object"');

var x74 = new Date(1999, 11, 31, 23);
assert.notSameValue(x74, undefined, 'The value of x74 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(1999, 12, 1, 0),
  "object",
  'The value of `typeof new Date(1999, 12, 1, 0)` is expected to be "object"'
);

assert.notSameValue(
  new Date(1999, 12, 1, 0),
  undefined,
  'new Date(1999, 12, 1, 0) is expected to not equal ``undefined``'
);

var x83 = new Date(1999, 12, 1, 0);
assert.sameValue(typeof x83, "object", 'The value of `typeof x83` is expected to be "object"');

var x84 = new Date(1999, 12, 1, 0);
assert.notSameValue(x84, undefined, 'The value of x84 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(2000, 0, 1, 0),
  "object",
  'The value of `typeof new Date(2000, 0, 1, 0)` is expected to be "object"'
);

assert.notSameValue(new Date(2000, 0, 1, 0), undefined, 'new Date(2000, 0, 1, 0) is expected to not equal ``undefined``');

var x93 = new Date(2000, 0, 1, 0);
assert.sameValue(typeof x93, "object", 'The value of `typeof x93` is expected to be "object"');

var x94 = new Date(2000, 0, 1, 0);
assert.notSameValue(x94, undefined, 'The value of x94 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(2099, 11, 31, 23),
  "object",
  'The value of `typeof new Date(2099, 11, 31, 23)` is expected to be "object"'
);

assert.notSameValue(
  new Date(2099, 11, 31, 23),
  undefined,
  'new Date(2099, 11, 31, 23) is expected to not equal ``undefined``'
);

var x103 = new Date(2099, 11, 31, 23);
assert.sameValue(typeof x103, "object", 'The value of `typeof x103` is expected to be "object"');

var x104 = new Date(2099, 11, 31, 23);
assert.notSameValue(x104, undefined, 'The value of x104 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(2099, 12, 1, 0),
  "object",
  'The value of `typeof new Date(2099, 12, 1, 0)` is expected to be "object"'
);

assert.notSameValue(
  new Date(2099, 12, 1, 0),
  undefined,
  'new Date(2099, 12, 1, 0) is expected to not equal ``undefined``'
);

var x113 = new Date(2099, 12, 1, 0);
assert.sameValue(typeof x113, "object", 'The value of `typeof x113` is expected to be "object"');

var x114 = new Date(2099, 12, 1, 0);
assert.notSameValue(x114, undefined, 'The value of x114 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(2100, 0, 1, 0),
  "object",
  'The value of `typeof new Date(2100, 0, 1, 0)` is expected to be "object"'
);

assert.notSameValue(new Date(2100, 0, 1, 0), undefined, 'new Date(2100, 0, 1, 0) is expected to not equal ``undefined``');

var x123 = new Date(2100, 0, 1, 0);
assert.sameValue(typeof x123, "object", 'The value of `typeof x123` is expected to be "object"');

var x124 = new Date(2100, 0, 1, 0);
assert.notSameValue(x124, undefined, 'The value of x124 is expected to not equal ``undefined``');
