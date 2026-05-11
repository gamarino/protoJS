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
    Compute the longest prefix of Result(2), which might be Result(2) itself,
    which satisfies the syntax of a StrDecimalLiteral
esid: sec-parsefloat-string
description: StrDecimalLiteral not contain HexIntegerLiteral
---*/

//CHECK#0
if (parseFloat("0x0") !== 0) {
  throw new Test262Error('#0: parseFloat("0x0") === 0. Actual: ' + (parseFloat("0x0")));
}

//CHECK#1
if (parseFloat("0x1") !== 0) {
  throw new Test262Error('#1: parseFloat("0x1") === 0. Actual: ' + (parseFloat("0x1")));
}

//CHECK#2
if (parseFloat("0x2") !== 0) {
  throw new Test262Error('#2: parseFloat("0x2") === 0. Actual: ' + (parseFloat("0x2")));
}

//CHECK#3
if (parseFloat("0x3") !== 0) {
  throw new Test262Error('#3: parseFloat("0x3") === 0. Actual: ' + (parseFloat("0x3")));
}

//CHECK#4
if (parseFloat("0x4") !== 0) {
  throw new Test262Error('#4: parseFloat("0x4") === 0. Actual: ' + (parseFloat("0x4")));
}

//CHECK#5
if (parseFloat("0x5") !== 0) {
  throw new Test262Error('#5: parseFloat("0x5") === 0. Actual: ' + (parseFloat("0x5")));
}

//CHECK#6
if (parseFloat("0x6") !== 0) {
  throw new Test262Error('#6: parseFloat("0x6") === 0. Actual: ' + (parseFloat("0x6")));
}

//CHECK#7
if (parseFloat("0x7") !== 0) {
  throw new Test262Error('#7: parseFloat("0x7") === 0. Actual: ' + (parseFloat("0x7")));
}

//CHECK#8
if (parseFloat("0x8") !== 0) {
  throw new Test262Error('#8: parseFloat("0x8") === 0. Actual: ' + (parseFloat("0x8")));
}

//CHECK#9
if (parseFloat("0x9") !== 0) {
  throw new Test262Error('#9: parseFloat("0x9") === 0. Actual: ' + (parseFloat("0x9")));
}

//CHECK#A
if (parseFloat("0xA") !== 0) {
  throw new Test262Error('#A: parseFloat("0xA") === 0. Actual: ' + (parseFloat("0xA")));
}

//CHECK#B
if (parseFloat("0xB") !== 0) {
  throw new Test262Error('#B: parseFloat("0xB") === 0. Actual: ' + (parseFloat("0xB")));
}

//CHECK#C
if (parseFloat("0xC") !== 0) {
  throw new Test262Error('#C: parseFloat("0xC") === 0. Actual: ' + (parseFloat("0xC")));
}

//CHECK#D
if (parseFloat("0xD") !== 0) {
  throw new Test262Error('#D: parseFloat("0xD") === 0. Actual: ' + (parseFloat("0xD")));
}

//CHECK#E
if (parseFloat("0xE") !== 0) {
  throw new Test262Error('#E: parseFloat("0xE") === 0. Actual: ' + (parseFloat("0xE")));
}

//CHECK#F
if (parseFloat("0xF") !== 0) {
  throw new Test262Error('#F: parseFloat("0xF") === 0. Actual: ' + (parseFloat("0xF")));
}
