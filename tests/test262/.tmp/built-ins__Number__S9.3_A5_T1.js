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
    Result of number conversion from object value is the result
    of conversion from primitive value
es5id: 9.3_A5_T1
description: >
    new Number(), new Number(0), new Number(Number.NaN), new
    Number(null),  new Number(void 0) and others convert to Number by
    explicit transformation
---*/
assert.sameValue(Number(new Number()), 0, 'Number(new Number()) must return 0');
assert.sameValue(Number(new Number(0)), 0, 'Number(new Number(0)) must return 0');

// CHECK#3
assert.sameValue(Number(new Number(NaN)), NaN, 'Number(new Number(NaN)) returns NaN');

assert.sameValue(Number(new Number(null)), 0, 'Number(new Number(null)) must return 0');

// CHECK#5
assert.sameValue(Number(new Number(void 0)), NaN, 'Number(new Number(void 0)) returns NaN');

assert.sameValue(Number(new Number(true)), 1, 'Number(new Number(true)) must return 1');
assert.sameValue(Number(new Number(false)), +0, 'Number(new Number(false)) must return +0');
assert.sameValue(Number(new Boolean(true)), 1, 'Number(new Boolean(true)) must return 1');
assert.sameValue(Number(new Boolean(false)), +0, 'Number(new Boolean(false)) must return +0');

// CHECK#10
assert.sameValue(Number(new Array(2, 4, 8, 16, 32)), NaN, 'Number(new Array(2, 4, 8, 16, 32)) returns NaN');

// CHECK#11
var myobj1 = {
  ToNumber: function() {
    return 12345;
  },
  toString: function() {
    return "67890";
  },
  valueOf: function() {
    return "[object MyObj]";
  }
};

assert.sameValue(Number(myobj1), NaN, 'Number("{ToNumber: function() {return 12345;}, toString: function() {return "67890";}, valueOf: function() {return "[object MyObj]";}}) returns NaN');

// CHECK#12
var myobj2 = {
  ToNumber: function() {
    return 12345;
  },
  toString: function() {
    return "67890";
  },
  valueOf: function() {
    return "9876543210";
  }
};

assert.sameValue(
  Number(myobj2),
  9876543210,
  'Number("{ToNumber: function() {return 12345;}, toString: function() {return "67890";}, valueOf: function() {return "9876543210";}}) must return 9876543210'
);


// CHECK#13
var myobj3 = {
  ToNumber: function() {
    return 12345;
  },
  toString: function() {
    return "[object MyObj]";
  }
};

assert.sameValue(Number(myobj3), NaN, 'Number("{ToNumber: function() {return 12345;}, toString: function() {return "[object MyObj]";}}) returns NaN');

// CHECK#14
var myobj4 = {
  ToNumber: function() {
    return 12345;
  },
  toString: function() {
    return "67890";
  }
};

assert.sameValue(
  Number(myobj4),
  67890,
  'Number("{ToNumber: function() {return 12345;}, toString: function() {return "67890";}}) must return 67890'
);

// CHECK#15
var myobj5 = {
  ToNumber: function() {
    return 12345;
  }
};

assert.sameValue(Number(myobj5), NaN, 'Number({ToNumber: function() {return 12345;}}) returns NaN');
