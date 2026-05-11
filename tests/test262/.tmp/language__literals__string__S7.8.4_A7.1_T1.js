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
    EscapeSequence :: UnicodeEscapeSequence :: u HexDigit HexDigit HexDigit
    HexDigit
es5id: 7.8.4_A7.1_T1
description: Check similar to ("\u0000" === String.fromCharCode("0"))
---*/

//CHECK#0
if ("\u0000" !== String.fromCharCode("0")) {
  throw new Test262Error('#0: "\\u0000" === String.fromCharCode("0")');
}

//CHECK#1
if ("\u0001" !== String.fromCharCode("1")) {
  throw new Test262Error('#1: "\\u0001" === String.fromCharCode("1")');
}

//CHECK#2
if ("\u0002" !== String.fromCharCode("2")) {
  throw new Test262Error('#2: "\\u0002" === String.fromCharCode("2")');
}

//CHECK#3
if ("\u0003" !== String.fromCharCode("3")) {
  throw new Test262Error('#3: "\\u0003" === String.fromCharCode("3")');
}

//CHECK#4
if ("\u0004" !== String.fromCharCode("4")) {
  throw new Test262Error('#4: "\\u0004" === String.fromCharCode("4")');
}

//CHECK#5
if ("\u0005" !== String.fromCharCode("5")) {
  throw new Test262Error('#5: "\\u0005" === String.fromCharCode("5")');
}

//CHECK#6
if ("\u0006" !== String.fromCharCode("6")) {
  throw new Test262Error('#6: "\\u0006" === String.fromCharCode("6")');
}

//CHECK#7
if ("\u0007" !== String.fromCharCode("7")) {
  throw new Test262Error('#7: "\\u0007" === String.fromCharCode("7")');
}

//CHECK#8
if ("\u0008" !== String.fromCharCode("8")) {
  throw new Test262Error('#8: "\\u0008" === String.fromCharCode("8")');
}

//CHECK#9
if ("\u0009" !== String.fromCharCode("9")) {
  throw new Test262Error('#9: "\\u0009" === String.fromCharCode("9")');
}

//CHECK#A
if ("\u000A" !== String.fromCharCode("10")) {
  throw new Test262Error('#A: "\\u000A" === String.fromCharCode("10")');
}

//CHECK#B
if ("\u000B" !== String.fromCharCode("11")) {
  throw new Test262Error('#B: "\\u000B" === String.fromCharCode("11")');
}

//CHECK#C
if ("\u000C" !== String.fromCharCode("12")) {
  throw new Test262Error('#C: "\\u000C" === String.fromCharCode("12")');
}

//CHECK#D
if ("\u000D" !== String.fromCharCode("13")) {
  throw new Test262Error('#D: "\\u000D" === String.fromCharCode("13")');
}

//CHECK#E
if ("\u000E" !== String.fromCharCode("14")) {
  throw new Test262Error('#E: "\\u000E" === String.fromCharCode("14")');
}

//CHECK#F
if ("\u000F" !== String.fromCharCode("15")) {
  throw new Test262Error('#F: "\\u000F" === String.fromCharCode("15")');
}
