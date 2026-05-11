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
esid: sec-array.prototype.tostring
info: |
    The result of calling this function is the same as if
    the built-in join method were invoked for this object with no argument
es5id: 15.4.4.2_A1_T3
description: Operator use ToString from array arguments
---*/

var x = new Array("", "", "");
if (x.toString() !== x.join()) {
  throw new Test262Error('#0.1: var x = new Array("","",""); x.toString() === x.join(). Actual: ' + (x.toString()));
} else {
  if (x.toString() !== ",,") {
    throw new Test262Error('#0.2: var x = new Array("","",""); x.toString() === ",,". Actual: ' + (x.toString()));
  }
}

var x = new Array("\\", "\\", "\\");
if (x.toString() !== x.join()) {
  throw new Test262Error('#1.1: var x = new Array("\\","\\","\\"); x.toString() === x.join(). Actual: ' + (x.toString()));
} else {
  if (x.toString() !== "\\,\\,\\") {
    throw new Test262Error('#1.2: var x = new Array("\\","\\","\\"); x.toString() === "\\,\\,\\". Actual: ' + (x.toString()));
  }
}

var x = new Array("&", "&", "&");
if (x.toString() !== x.join()) {
  throw new Test262Error('#2.1: var x = new Array("&", "&", "&"); x.toString() === x.join(). Actual: ' + (x.toString()));
} else {
  if (x.toString() !== "&,&,&") {
    throw new Test262Error('#2.2: var x = new Array("&", "&", "&"); x.toString() === "&,&,&". Actual: ' + (x.toString()));
  }
}

var x = new Array(true, true, true);
if (x.toString() !== x.join()) {
  throw new Test262Error('#3.1: var x = new Array(true,true,true); x.toString(true,true,true) === x.join(). Actual: ' + (x.toString(true, true, true)));
} else {
  if (x.toString() !== "true,true,true") {
    throw new Test262Error('#3.2: var x = new Array(true,true,true); x.toString(true,true,true) === "true,true,true". Actual: ' + (x.toString(true, true, true)));
  }
}

var x = new Array(null, null, null);
if (x.toString() !== x.join()) {
  throw new Test262Error('#4.1: var x = new Array(null,null,null); x.toString(null,null,null) === x.join(). Actual: ' + (x.toString(null, null, null)));
} else {
  if (x.toString() !== ",,") {
    throw new Test262Error('#4.2: var x = new Array(null,null,null); x.toString(null,null,null) === ",,". Actual: ' + (x.toString(null, null, null)));
  }
}

var x = new Array(undefined, undefined, undefined);
if (x.toString() !== x.join()) {
  throw new Test262Error('#5.1: var x = new Array(undefined,undefined,undefined); x.toString(undefined,undefined,undefined) === x.join(). Actual: ' + (x.toString(undefined, undefined, undefined)));
} else {
  if (x.toString() !== ",,") {
    throw new Test262Error('#5.2: var x = new Array(undefined,undefined,undefined); x.toString(undefined,undefined,undefined) === ",,". Actual: ' + (x.toString(undefined, undefined, undefined)));
  }
}

var x = new Array(Infinity, Infinity, Infinity);
if (x.toString() !== x.join()) {
  throw new Test262Error('#6.1: var x = new Array(Infinity,Infinity,Infinity); x.toString(Infinity,Infinity,Infinity) === x.join(). Actual: ' + (x.toString(Infinity, Infinity, Infinity)));
} else {
  if (x.toString() !== "Infinity,Infinity,Infinity") {
    throw new Test262Error('#6.2: var x = new Array(Infinity,Infinity,Infinity); x.toString(Infinity,Infinity,Infinity) === "Infinity,Infinity,Infinity". Actual: ' + (x.toString(Infinity, Infinity, Infinity)));
  }
}

var x = new Array(NaN, NaN, NaN);
if (x.toString() !== x.join()) {
  throw new Test262Error('#7.1: var x = new Array(NaN,NaN,NaN); x.toString(NaN,NaN,NaN) === x.join(). Actual: ' + (x.toString(NaN, NaN, NaN)));
} else {
  if (x.toString() !== "NaN,NaN,NaN") {
    throw new Test262Error('#7.2: var x = new Array(NaN,NaN,NaN); x.toString(NaN,NaN,NaN) === "NaN,NaN,NaN". Actual: ' + (x.toString(NaN, NaN, NaN)));
  }
}
