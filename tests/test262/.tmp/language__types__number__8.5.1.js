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


// Copyright (c) 2012 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
es5id: 8.5.1
description: Valid Number ranges
---*/

// Check range support for Number values (IEEE 754 64-bit floats having the form s*m*2**e)
//
// For normalized floats, sign (s) is +1 or -1, m (mantisa) is a positive integer less
// than 2**53 but not less than 2**52 and e (exponent) is an integer ranging from -1074 to 971
//
// For denormalized floats, s is +1 or -1, m is a positive integer less than 2**52, and
// e is -1074
//
// Below 64-bit float values shown for informational purposes.  Values may be positive or negative.
// Infinity  >= ~1.797693134862315907729305190789e+308 >= 2**1024
// MAX_NORM   = ~1.797693134862315708145274237317e+308  = (2**53 - 1) * (2**-52) * (2**1023) = (2**53-1) * (2**971) = (2**1024) - (2**971)
// MIN_NORM   = ~2.2250738585072013830902327173324e-308 = 2**-1022
// MAX_DENORM = ~2.2250738585072008890245868760859e-308 = MIN_NORM - MIN_DENORM = (2**-1022) - (2**-1074)
// MIN_DENORM = ~4.9406564584124654417656879286822e-324 = 2**-1074

// Fill an array with 2 to the power of (0 ... -1075)
var value = 1;
var floatValues = new Array(1076);
for(var power = 0; power <= 1075; power++){
	floatValues[power] = value;
    // Use basic math operations for testing, which are required to support 'gradual underflow' rather
    // than Math.pow etc..., which are defined as 'implementation dependent'.
	value = value * 0.5;
}

// The last value is below min denorm and should round to 0, everything else should contain a value
if(floatValues[1075] !== 0) {
  throw new Test262Error("Value after min denorm should round to 0");
}

// Validate the last actual value is min denorm
if(floatValues[1074] !== 4.9406564584124654417656879286822e-324) {
  throw new Test262Error("Min denorm value is incorrect: " + floatValues[1074]);
}

// Validate that every value is half the value before it up to 1
for(var index = 1074; index > 0; index--){
  if(floatValues[index] === 0){
	throw new Test262Error("2**-" + index + " should not be 0");
  }
  if(floatValues[index - 1] !== (floatValues[index] * 2)){
	throw new Test262Error("Value should be double adjacent value at index " + index);
  }
}

// Max norm should be supported and compare less than inifity
if(!(1.797693134862315708145274237317e+308 < Infinity)){
	throw new Test262Error("Max Number value 1.797693134862315708145274237317e+308 should not overflow to infinity");
}

// Numbers closer to 2**1024 then max norm should overflow to infinity
if(!(1.797693134862315808e+308 === +Infinity)){
	throw new Test262Error("1.797693134862315808e+308 did not resolve to Infinity");
}
