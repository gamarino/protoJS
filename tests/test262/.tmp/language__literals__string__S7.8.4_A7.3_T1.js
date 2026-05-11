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
info: \u HexDigit HexDigit HexDigit HexDigit DoubleStringCharacter
es5id: 7.8.4_A7.3_T1
description: Check similar to ("\u0001F" === String.fromCharCode("1") + "F")
---*/

//CHECK#1
if ("\u0001F" !== String.fromCharCode("1") + "F") {
  throw new Test262Error('#1: "\\u0001F" === String.fromCharCode("1") + "F"');
}

//CHECK#2
if ("\u0002E" !== String.fromCharCode("2") + "E") {
  throw new Test262Error('#2: "\\u0002E" === String.fromCharCode("2") + "E"');
}

//CHECK#3
if ("\u0003D" !== String.fromCharCode("3") + "D") {
  throw new Test262Error('#3: "\\u0003D" === String.fromCharCode("3") + "D"');
}

//CHECK#4
if ("\u0004C" !== String.fromCharCode("4") + "C") {
  throw new Test262Error('#4: "\\u0004C" === String.fromCharCode("4") + "C"');
}

//CHECK#5
if ("\u0005B" !== String.fromCharCode("5") + "B") {
  throw new Test262Error('#5: "\\u0005B" === String.fromCharCode("5") + "B"');
}

//CHECK#6
if ("\u0006A" !== String.fromCharCode("6") + "A") {
  throw new Test262Error('#6: "\\u0006A" === String.fromCharCode("6") + "A"');
}

//CHECK#7
if ("\u00079" !== String.fromCharCode("7") + "9") {
  throw new Test262Error('#7: "\\u00079" === String.fromCharCode("7") + "9"');
}

//CHECK#8
if ("\u00088" !== String.fromCharCode("8") + "8") {
  throw new Test262Error('#8: "\\u00088" === String.fromCharCode("8") + "8"');
}

//CHECK#9
if ("\u00097" !== String.fromCharCode("9") + "7") {
  throw new Test262Error('#9: "\\u00097" === String.fromCharCode("9") + "7"');
}

//CHECK#A
if ("\u000A6" !== String.fromCharCode("10") + "6") {
  throw new Test262Error('#A: "\\u000A6" === String.fromCharCode("10") + "6"');
}

//CHECK#B
if ("\u000B5" !== String.fromCharCode("11") + "5") {
  throw new Test262Error('#B: "\\u000B5" === String.fromCharCode("11") + "5"');
}

//CHECK#C
if ("\u000C4" !== String.fromCharCode("12") + "4") {
  throw new Test262Error('#C: "\\u000C4" === String.fromCharCode("12") + "4"');
}

//CHECK#D
if ("\u000D3" !== String.fromCharCode("13") + "3") {
  throw new Test262Error('#D: "\\u000D3" === String.fromCharCode("13") + "3"');
}

//CHECK#E
if ("\u000E2" !== String.fromCharCode("14") + "2") {
  throw new Test262Error('#E: "\\u000E2" === String.fromCharCode("14") + "2"');
}

//CHECK#F
if ("\u000F1" !== String.fromCharCode("15") + "1") {
  throw new Test262Error('#F: "\\u000F1" === String.fromCharCode("15") + "1"');
}
