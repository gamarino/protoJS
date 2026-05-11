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


// Copyright (C) 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Collection of date-centric values
defines:
  - date_1899_end
  - date_1900_start
  - date_1969_end
  - date_1970_start
  - date_1999_end
  - date_2000_start
  - date_2099_end
  - date_2100_start
  - start_of_time
  - end_of_time
---*/

var date_1899_end = -2208988800001;
var date_1900_start = -2208988800000;
var date_1969_end = -1;
var date_1970_start = 0;
var date_1999_end = 946684799999;
var date_2000_start = 946684800000;
var date_2099_end = 4102444799999;
var date_2100_start = 4102444800000;

var start_of_time = -8.64e15;
var end_of_time = 8.64e15;


// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: |
    When Date is called as part of a new expression it is
    a constructor: it initialises the newly created object
esid: sec-date-value
description: Checking types of newly created objects and it values
includes: [dateConstants.js]
---*/
assert.sameValue(
  typeof new Date(date_1899_end),
  "object",
  'The value of `typeof new Date(date_1899_end)` is expected to be "object"'
);

assert.notSameValue(new Date(date_1899_end), undefined, 'new Date(date_1899_end) is expected to not equal ``undefined``');

var x13 = new Date(date_1899_end);
assert.sameValue(typeof x13, "object", 'The value of `typeof x13` is expected to be "object"');

var x14 = new Date(date_1899_end);
assert.notSameValue(x14, undefined, 'The value of x14 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(date_1900_start),
  "object",
  'The value of `typeof new Date(date_1900_start)` is expected to be "object"'
);

assert.notSameValue(
  new Date(date_1900_start),
  undefined,
  'new Date(date_1900_start) is expected to not equal ``undefined``'
);

var x23 = new Date(date_1900_start);
assert.sameValue(typeof x23, "object", 'The value of `typeof x23` is expected to be "object"');

var x24 = new Date(date_1900_start);
assert.notSameValue(x24, undefined, 'The value of x24 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(date_1969_end),
  "object",
  'The value of `typeof new Date(date_1969_end)` is expected to be "object"'
);

assert.notSameValue(new Date(date_1969_end), undefined, 'new Date(date_1969_end) is expected to not equal ``undefined``');

var x33 = new Date(date_1969_end);
assert.sameValue(typeof x33, "object", 'The value of `typeof x33` is expected to be "object"');

var x34 = new Date(date_1969_end);
assert.notSameValue(x34, undefined, 'The value of x34 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(date_1970_start),
  "object",
  'The value of `typeof new Date(date_1970_start)` is expected to be "object"'
);

assert.notSameValue(
  new Date(date_1970_start),
  undefined,
  'new Date(date_1970_start) is expected to not equal ``undefined``'
);

var x43 = new Date(date_1970_start);
assert.sameValue(typeof x43, "object", 'The value of `typeof x43` is expected to be "object"');

var x44 = new Date(date_1970_start);
assert.notSameValue(x44, undefined, 'The value of x44 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(date_1999_end),
  "object",
  'The value of `typeof new Date(date_1999_end)` is expected to be "object"'
);

assert.notSameValue(new Date(date_1999_end), undefined, 'new Date(date_1999_end) is expected to not equal ``undefined``');

var x53 = new Date(date_1999_end);
assert.sameValue(typeof x53, "object", 'The value of `typeof x53` is expected to be "object"');

var x54 = new Date(date_1999_end);
assert.notSameValue(x54, undefined, 'The value of x54 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(date_2000_start),
  "object",
  'The value of `typeof new Date(date_2000_start)` is expected to be "object"'
);

assert.notSameValue(
  new Date(date_2000_start),
  undefined,
  'new Date(date_2000_start) is expected to not equal ``undefined``'
);

var x63 = new Date(date_2000_start);
assert.sameValue(typeof x63, "object", 'The value of `typeof x63` is expected to be "object"');

var x64 = new Date(date_2000_start);
assert.notSameValue(x64, undefined, 'The value of x64 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(date_2099_end),
  "object",
  'The value of `typeof new Date(date_2099_end)` is expected to be "object"'
);

assert.notSameValue(new Date(date_2099_end), undefined, 'new Date(date_2099_end) is expected to not equal ``undefined``');

var x73 = new Date(date_2099_end);
assert.sameValue(typeof x73, "object", 'The value of `typeof x73` is expected to be "object"');

var x74 = new Date(date_2099_end);
assert.notSameValue(x74, undefined, 'The value of x74 is expected to not equal ``undefined``');

assert.sameValue(
  typeof new Date(date_2100_start),
  "object",
  'The value of `typeof new Date(date_2100_start)` is expected to be "object"'
);

assert.notSameValue(
  new Date(date_2100_start),
  undefined,
  'new Date(date_2100_start) is expected to not equal ``undefined``'
);

var x83 = new Date(date_2100_start);
assert.sameValue(typeof x83, "object", 'The value of `typeof x83` is expected to be "object"');

var x84 = new Date(date_2100_start);
assert.notSameValue(x84, undefined, 'The value of x84 is expected to not equal ``undefined``');
