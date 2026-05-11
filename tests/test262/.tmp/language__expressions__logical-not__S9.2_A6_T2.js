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
info: Result of boolean conversion from object is true
es5id: 9.2_A6_T2
description: Different objects convert to Boolean by implicit transformation
---*/

// CHECK#1
if (!(new Object()) !== false) {
  throw new Test262Error('#1: !(new Object()) === false. Actual: ' + (!(new Object())));	
}

// CHECK#2
if (!(new String("")) !== false) {
  throw new Test262Error('#2: !(new String("")) === false. Actual: ' + (!(new String(""))));	
}

// CHECK#3
if (!(new String()) !== false) {
  throw new Test262Error('#3: !(new String()) === false. Actual: ' + (!(new String())));	
}

// CHECK#4
if (!(new Boolean(true)) !== false) {
  throw new Test262Error('#4: !(new Boolean(true)) === false. Actual: ' + (!(new Boolean(true))));	
}

// CHECK#5
if (!(new Boolean(false)) !== false) {
  throw new Test262Error('#5: !(new Boolean(false)) === false. Actual: ' + (!(new Boolean(false))));	
}

// CHECK#6
if (!(new Boolean()) !== false) {
  throw new Test262Error('#6: !(new Boolean()) === false. Actual: ' + (!(new Boolean())));	
}

// CHECK#7
if (!(new Array()) !== false) {
  throw new Test262Error('#7: !(new Array()) === false. Actual: ' + (!(new Array())));	
}

// CHECK#8
if (!(new Number()) !== false) {
  throw new Test262Error('#8: !(new Number()) === false. Actual: ' + (!(new Number())));	
}

// CHECK#9
if (!(new Number(-0)) !== false) {
  throw new Test262Error('#9: !(new Number(-0)) === false. Actual: ' + (!(new Number(-0))));	
}

// CHECK#10
if (!(new Number(0)) !== false) {
  throw new Test262Error('#10: !(new Number(0)) === false. Actual: ' + (!(new Number(0))));	
}

// CHECK#11
if (!(new Number()) !== false) {
  throw new Test262Error('#11: !(new Number()) === false. Actual: ' + (!(new Number())));	
}

// CHECK#12
if (!(new Number(Number.NaN)) !== false) {
  throw new Test262Error('#12: !(new Number(Number.NaN)) === false. Actual: ' + (!(new Number(Number.NaN))));	
}

// CHECK#13
if (!(new Number(-1)) !== false) {
  throw new Test262Error('#13: !(new Number(-1)) === false. Actual: ' + (!(new Number(-1))));	
}

// CHECK#14
if (!(new Number(1)) !== false) {
  throw new Test262Error('#14: !(new Number(1)) === false. Actual: ' + (!(new Number(1))));	
}

// CHECK#15
if (!(new Number(Number.POSITIVE_INFINITY)) !== false) {
  throw new Test262Error('#15: !(new Number(Number.POSITIVE_INFINITY)) === false. Actual: ' + (!(new Number(Number.POSITIVE_INFINITY))));	
}

// CHECK#16
if (!(new Number(Number.NEGATIVE_INFINITY)) !== false) {
  throw new Test262Error('#16: !(new Number(Number.NEGATIVE_INFINITY)) === false. Actual: ' + (!(new Number(Number.NEGATIVE_INFINITY))));	
}

// CHECK#17
if (!(new Function()) !== false) {
  throw new Test262Error('#17: !(new Function()) === false. Actual: ' + (!(new Function())));	
}

// CHECK#18
if (!(new Date()) !== false) {
  throw new Test262Error('#18: !(new Date()) === false. Actual: ' + (!(new Date())));	
}

// CHECK#19
if (!(new Date(0)) !== false) {
  throw new Test262Error('#19: !(new Date(0)) === false. Actual: ' + (!(new Date(0))));	
}
