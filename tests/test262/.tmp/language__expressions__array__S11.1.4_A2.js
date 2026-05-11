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
info: Create multi dimensional array
es5id: 11.1.4_A2
description: >
    Checking various properteis and contents of the arrya defined with
    "var array = [[1,2], [3], []]"
---*/

var array = [[1,2], [3], []];

//CHECK#1
if (typeof array !== "object") {
  throw new Test262Error('#1: var array = [[1,2], [3], []]; typeof array === "object". Actual: ' + (typeof array));
}

//CHECK#2
if (array instanceof Array !== true) {
  throw new Test262Error('#2: var array = [[1,2], [3], []]; array instanceof Array === true');
}

//CHECK#3
if (array.toString !== Array.prototype.toString) {
  throw new Test262Error('#3: var array = [[1,2], [3], []]; array.toString === Array.prototype.toString. Actual: ' + (array.toString));
}

//CHECK#4
if (array.length !== 3) {
  throw new Test262Error('#4: var array = [[1,2], [3], []]; array.length === 3. Actual: ' + (array.length));
}

var subarray = array[0];

//CHECK#5
if (typeof subarray !== "object") {
  throw new Test262Error('#5: var array = [[1,2], [3], []]; var subarray = array[0]; typeof subarray === "object". Actual: ' + (typeof subarray));
}

//CHECK#6
if (subarray instanceof Array !== true) {
  throw new Test262Error('#6: var array = [[1,2], [3], []]; var subarray = array[0]; subarray instanceof Array === true');
}

//CHECK#7
if (subarray.toString !== Array.prototype.toString) {
  throw new Test262Error('#7: var array = [[1,2], [3], []]; var subarray = array[0]; subarray.toString === Array.prototype.toString. Actual: ' + (subarray.toString));
}

//CHECK#8
if (subarray.length !== 2) {
  throw new Test262Error('#8: var array = [[1,2], [3], []]; var subarray = array[0]; subarray.length === 2. Actual: ' + (subarray.length));
}

//CHECK#9
if (subarray[0] !== 1) {
  throw new Test262Error('#9: var array = [[1,2], [3], []]; var subarray = array[0]; subarray[0] === 1. Actual: ' + (subarray[0]));
}

//CHECK#10
if (subarray[1] !== 2) {
  throw new Test262Error('#10: var array = [[1,2], [3], []]; var subarray = array[1]; subarray[1] === 2. Actual: ' + (subarray[1]));
}

var subarray = array[1];

//CHECK#11
if (typeof subarray !== "object") {
throw new Test262Error('#11: var array = [[1,2], [3], []]; var subarray = array[1]; typeof subarray === "object". Actual: ' + (typeof subarray));
}

//CHECK#12
if (subarray instanceof Array !== true) {
throw new Test262Error('#12: var array = [[1,2], [3], []]; var subarray = array[1]; subarray instanceof Array === true');
}

//CHECK#13
if (subarray.toString !== Array.prototype.toString) {
throw new Test262Error('#13: var array = [[1,2], [3], []]; var subarray = array[1]; subarray.toString === Array.prototype.toString. Actual: ' + (subarray.toString));
}

//CHECK#14
if (subarray.length !== 1) {
throw new Test262Error('#14: var array = [[1,2], [3], []]; var subarray = array[1]; subarray.length === 1. Actual: ' + (subarray.length));
}

//CHECK#15
if (subarray[0] !== 3) {
throw new Test262Error('#15: var array = [[1,2], [3], []]; var subarray = array[1]; subarray[0] === 3. Actual: ' + (subarray[0]));
}

var subarray = array[2];

//CHECK#16
if (typeof subarray !== "object") {
throw new Test262Error('#16: var array = [[1,2], [3], []]; var subarray = array[2]; typeof subarray === "object". Actual: ' + (typeof subarray));
}

//CHECK#17
if (subarray instanceof Array !== true) {
throw new Test262Error('#17: var array = [[1,2], [3], []]; var subarray = array[2]; subarray instanceof Array === true');
}

//CHECK#18
if (subarray.toString !== Array.prototype.toString) {
throw new Test262Error('#18: var array = [[1,2], [3], []]; var subarray = array[2]; subarray.toString === Array.prototype.toString. Actual: ' + (subarray.toString));
}

//CHECK#19
if (subarray.length !== 0) {
throw new Test262Error('#19: var array = [[1,2], [3], []]; var subarray = array[2]; subarray.length === 0. Actual: ' + (subarray.length));
}

//CHECK#20
if (array[0][0] !== 1) {
  throw new Test262Error('#20: var array = [[1,2], [3], []]; array[0][0] === 1. Actual: ' + (array[0][0]));
}

//CHECK#21
if (array[0][1] !== 2) {
  throw new Test262Error('#21: var array = [[1,2], [3], []]; array[0][1] === 2. Actual: ' + (array[0][1]));
}

//CHECK#22
if (array[1][0] !== 3) {
  throw new Test262Error('#722: var array = [[1,2], [3], []]; array[1][0] === 3. Actual: ' + (array[1][0]));
}
