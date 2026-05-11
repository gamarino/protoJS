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
info: Check type of various properties
es5id: 11.2.1_A4_T9
description: Checking properties of the Date object
---*/

//CHECK#1-86
if (typeof Date.parse !== "function")  throw new Test262Error('#1: typeof Date.parse === "function". Actual: ' + (typeof Date.parse ));
if (typeof Date['parse'] !== "function")  throw new Test262Error('#2: typeof Date["parse"] === "function". Actual: ' + (typeof Date["parse"] ));
if (typeof Date.prototype !== "object")  throw new Test262Error('#3: typeof Date.prototype === "object". Actual: ' + (typeof Date.prototype ));
if (typeof Date['prototype'] !== "object")  throw new Test262Error('#4: typeof Date["prototype"] === "object". Actual: ' + (typeof Date["prototype"] ));
if (typeof Date.UTC !== "function")  throw new Test262Error('#5: typeof Date.UTC === "function". Actual: ' + (typeof Date.UTC ));
if (typeof Date['UTC'] !== "function")  throw new Test262Error('#6: typeof Date["UTC"] === "function". Actual: ' + (typeof Date["UTC"] ));
if (typeof Date.prototype.constructor !== "function")  throw new Test262Error('#7: typeof Date.prototype.constructor === "funvtion". Actual: ' + (typeof Date.prototype.constructor ));
if (typeof Date.prototype['constructor'] !== "function")  throw new Test262Error('#8: typeof Date.prototype["constructor"] === "function". Actual: ' + (typeof Date.prototype["constructor"] ));
if (typeof Date.prototype.toString !== "function")  throw new Test262Error('#9: typeof Date.prototype.toString === "function". Actual: ' + (typeof Date.prototype.toString ));
if (typeof Date.prototype['toString'] !== "function")  throw new Test262Error('#10: typeof Date.prototype["toString"] === "function". Actual: ' + (typeof Date.prototype["toString"] ));
if (typeof Date.prototype.valueOf !== "function")  throw new Test262Error('#11: typeof Date.prototype.valueOf === "function". Actual: ' + (typeof Date.prototype.valueOf ));
if (typeof Date.prototype['valueOf'] !== "function")  throw new Test262Error('#12: typeof Date.prototype["valueOf"] === "function". Actual: ' + (typeof Date.prototype["valueOf"] ));
if (typeof Date.prototype.getTime !== "function")  throw new Test262Error('#13: typeof Date.prototype.getTime === "function". Actual: ' + (typeof Date.prototype.getTime ));
if (typeof Date.prototype['getTime'] !== "function")  throw new Test262Error('#14: typeof Date.prototype["getTime"] === "function". Actual: ' + (typeof Date.prototype["getTime"] ));
if (typeof Date.prototype.getFullYear !== "function")  throw new Test262Error('#17: typeof Date.prototype.getFullYear === "function". Actual: ' + (typeof Date.prototype.getFullYear ));
if (typeof Date.prototype['getFullYear'] !== "function")  throw new Test262Error('#18: typeof Date.prototype["getFullYear"] === "function". Actual: ' + (typeof Date.prototype["getFullYear"] ));
if (typeof Date.prototype.getUTCFullYear !== "function")  throw new Test262Error('#19: typeof Date.prototype.getUTCFullYear === "function". Actual: ' + (typeof Date.prototype.getUTCFullYear ));
if (typeof Date.prototype['getUTCFullYear'] !== "function")  throw new Test262Error('#20: typeof Date.prototype["getUTCFullYear"] === "function". Actual: ' + (typeof Date.prototype["getUTCFullYear"] ));
if (typeof Date.prototype.getMonth !== "function")  throw new Test262Error('#21: typeof Date.prototype.getMonth === "function". Actual: ' + (typeof Date.prototype.getMonth ));
if (typeof Date.prototype['getMonth'] !== "function")  throw new Test262Error('#22: typeof Date.prototype["getMonth"] === "function". Actual: ' + (typeof Date.prototype["getMonth"] ));
if (typeof Date.prototype.getUTCMonth !== "function")  throw new Test262Error('#23: typeof Date.prototype.getUTCMonth === "function". Actual: ' + (typeof Date.prototype.getUTCMonth ));
if (typeof Date.prototype['getUTCMonth'] !== "function")  throw new Test262Error('#24: typeof Date.prototype["getUTCMonth"] === "function". Actual: ' + (typeof Date.prototype["getUTCMonth"] ));
if (typeof Date.prototype.getDate !== "function")  throw new Test262Error('#25: typeof Date.prototype.getDate === "function". Actual: ' + (typeof Date.prototype.getDate ));
if (typeof Date.prototype['getDate'] !== "function")  throw new Test262Error('#26: typeof Date.prototype["getDate"] === "function". Actual: ' + (typeof Date.prototype["getDate"] ));
if (typeof Date.prototype.getUTCDate !== "function")  throw new Test262Error('#27: typeof Date.prototype.getUTCDate === "function". Actual: ' + (typeof Date.prototype.getUTCDate ));
if (typeof Date.prototype['getUTCDate'] !== "function")  throw new Test262Error('#28: typeof Date.prototype["getUTCDate"] === "function". Actual: ' + (typeof Date.prototype["getUTCDate"] ));
if (typeof Date.prototype.getDay !== "function")  throw new Test262Error('#29: typeof Date.prototype.getDay === "function". Actual: ' + (typeof Date.prototype.getDay ));
if (typeof Date.prototype['getDay'] !== "function")  throw new Test262Error('#30: typeof Date.prototype["getDay"] === "function". Actual: ' + (typeof Date.prototype["getDay"] ));
if (typeof Date.prototype.getUTCDay !== "function")  throw new Test262Error('#31: typeof Date.prototype.getUTCDay === "function". Actual: ' + (typeof Date.prototype.getUTCDay ));
if (typeof Date.prototype['getUTCDay'] !== "function")  throw new Test262Error('#32: typeof Date.prototype["getUTCDay"] === "function". Actual: ' + (typeof Date.prototype["getUTCDay"] ));
if (typeof Date.prototype.getHours !== "function")  throw new Test262Error('#33: typeof Date.prototype.getHours === "function". Actual: ' + (typeof Date.prototype.getHours ));
if (typeof Date.prototype['getHours'] !== "function")  throw new Test262Error('#34: typeof Date.prototype["getHours"] === "function". Actual: ' + (typeof Date.prototype["getHours"] ));
if (typeof Date.prototype.getUTCHours !== "function")  throw new Test262Error('#35: typeof Date.prototype.getUTCHours === "function". Actual: ' + (typeof Date.prototype.getUTCHours ));
if (typeof Date.prototype['getUTCHours'] !== "function")  throw new Test262Error('#36: typeof Date.prototype["getUTCHours"] === "function". Actual: ' + (typeof Date.prototype["getUTCHours"] ));
if (typeof Date.prototype.getMinutes !== "function")  throw new Test262Error('#37: typeof Date.prototype.getMinutes === "function". Actual: ' + (typeof Date.prototype.getMinutes ));
if (typeof Date.prototype['getMinutes'] !== "function")  throw new Test262Error('#38: typeof Date.prototype["getMinutes"] === "function". Actual: ' + (typeof Date.prototype["getMinutes"] ));
if (typeof Date.prototype.getUTCMinutes !== "function")  throw new Test262Error('#39: typeof Date.prototype.getUTCMinutes === "function". Actual: ' + (typeof Date.prototype.getUTCMinutes ));
if (typeof Date.prototype['getUTCMinutes'] !== "function")  throw new Test262Error('#40: typeof Date.prototype["getUTCMinutes"] === "function". Actual: ' + (typeof Date.prototype["getUTCMinutes"] ));
if (typeof Date.prototype.getSeconds !== "function")  throw new Test262Error('#41: typeof Date.prototype.getSeconds === "function". Actual: ' + (typeof Date.prototype.getSeconds ));
if (typeof Date.prototype['getSeconds'] !== "function")  throw new Test262Error('#42: typeof Date.prototype["getSeconds"] === "function". Actual: ' + (typeof Date.prototype["getSeconds"] ));
if (typeof Date.prototype.getUTCSeconds !== "function")  throw new Test262Error('#43: typeof Date.prototype.getUTCSeconds === "function". Actual: ' + (typeof Date.prototype.getUTCSeconds ));
if (typeof Date.prototype['getUTCSeconds'] !== "function")  throw new Test262Error('#44: typeof Date.prototype["getUTCSeconds"] === "function". Actual: ' + (typeof Date.prototype["getUTCSeconds"] ));
if (typeof Date.prototype.getMilliseconds !== "function")  throw new Test262Error('#45: typeof Date.prototype.getMilliseconds === "function". Actual: ' + (typeof Date.prototype.getMilliseconds ));
if (typeof Date.prototype['getMilliseconds'] !== "function")  throw new Test262Error('#46: typeof Date.prototype["getMilliseconds"] === "function". Actual: ' + (typeof Date.prototype["getMilliseconds"] ));
if (typeof Date.prototype.getUTCMilliseconds !== "function")  throw new Test262Error('#47: typeof Date.prototype.getUTCMilliseconds === "function". Actual: ' + (typeof Date.prototype.getUTCMilliseconds ));
if (typeof Date.prototype['getUTCMilliseconds'] !== "function")  throw new Test262Error('#48: typeof Date.prototype["getUTCMilliseconds"] === "function". Actual: ' + (typeof Date.prototype["getUTCMilliseconds"] ));
if (typeof Date.prototype.setTime !== "function")  throw new Test262Error('#49: typeof Date.prototype.setTime === "function". Actual: ' + (typeof Date.prototype.setTime ));
if (typeof Date.prototype['setTime'] !== "function")  throw new Test262Error('#50: typeof Date.prototype["setTime"] === "function". Actual: ' + (typeof Date.prototype["setTime"] ));
if (typeof Date.prototype.setMilliseconds !== "function")  throw new Test262Error('#51: typeof Date.prototype.setMilliseconds === "function". Actual: ' + (typeof Date.prototype.setMilliseconds ));
if (typeof Date.prototype['setMilliseconds'] !== "function")  throw new Test262Error('#52: typeof Date.prototype["setMilliseconds"] === "function". Actual: ' + (typeof Date.prototype["setMilliseconds"] ));
if (typeof Date.prototype.setUTCMilliseconds !== "function")  throw new Test262Error('#53: typeof Date.prototype.setUTCMilliseconds === "function". Actual: ' + (typeof Date.prototype.setUTCMilliseconds ));
if (typeof Date.prototype['setUTCMilliseconds'] !== "function")  throw new Test262Error('#54: typeof Date.prototype["setUTCMilliseconds"] === "function". Actual: ' + (typeof Date.prototype["setUTCMilliseconds"] ));
if (typeof Date.prototype.setSeconds !== "function")  throw new Test262Error('#55: typeof Date.prototype.setSeconds === "function". Actual: ' + (typeof Date.prototype.setSeconds ));
if (typeof Date.prototype['setSeconds'] !== "function")  throw new Test262Error('#56: typeof Date.prototype["setSeconds"] === "function". Actual: ' + (typeof Date.prototype["setSeconds"] ));
if (typeof Date.prototype.setUTCSeconds !== "function")  throw new Test262Error('#57: typeof Date.prototype.setUTCSeconds === "function". Actual: ' + (typeof Date.prototype.setUTCSeconds ));
if (typeof Date.prototype['setUTCSeconds'] !== "function")  throw new Test262Error('#58: typeof Date.prototype["setUTCSeconds"] === "function". Actual: ' + (typeof Date.prototype["setUTCSeconds"] ));
if (typeof Date.prototype.setMinutes !== "function")  throw new Test262Error('#59: typeof Date.prototype.setMinutes === "function". Actual: ' + (typeof Date.prototype.setMinutes ));
if (typeof Date.prototype['setMinutes'] !== "function")  throw new Test262Error('#60: typeof Date.prototype["setMinutes"] === "function". Actual: ' + (typeof Date.prototype["setMinutes"] ));
if (typeof Date.prototype.setUTCMinutes !== "function")  throw new Test262Error('#61: typeof Date.prototype.setUTCMinutes === "function". Actual: ' + (typeof Date.prototype.setUTCMinutes ));
if (typeof Date.prototype['setUTCMinutes'] !== "function")  throw new Test262Error('#62: typeof Date.prototype["setUTCMinutes"] === "function". Actual: ' + (typeof Date.prototype["setUTCMinutes"] ));
if (typeof Date.prototype.setHours !== "function")  throw new Test262Error('#63: typeof Date.prototype.setHours === "function". Actual: ' + (typeof Date.prototype.setHours ));
if (typeof Date.prototype['setHours'] !== "function")  throw new Test262Error('#64: typeof Date.prototype["setHours"] === "function". Actual: ' + (typeof Date.prototype["setHours"] ));
if (typeof Date.prototype.setUTCHours !== "function")  throw new Test262Error('#65: typeof Date.prototype.setUTCHours === "function". Actual: ' + (typeof Date.prototype.setUTCHours ));
if (typeof Date.prototype['setUTCHours'] !== "function")  throw new Test262Error('#66: typeof Date.prototype["setUTCHours"] === "function". Actual: ' + (typeof Date.prototype["setUTCHours"] ));
if (typeof Date.prototype.setDate !== "function")  throw new Test262Error('#67: typeof Date.prototype.setDate === "function". Actual: ' + (typeof Date.prototype.setDate ));
if (typeof Date.prototype['setDate'] !== "function")  throw new Test262Error('#68: typeof Date.prototype["setDate"] === "function". Actual: ' + (typeof Date.prototype["setDate"] ));
if (typeof Date.prototype.setUTCDate !== "function")  throw new Test262Error('#69: typeof Date.prototype.setUTCDate === "function". Actual: ' + (typeof Date.prototype.setUTCDate ));
if (typeof Date.prototype['setUTCDate'] !== "function")  throw new Test262Error('#70: typeof Date.prototype["setUTCDate"] === "function". Actual: ' + (typeof Date.prototype["setUTCDate"] ));
if (typeof Date.prototype.setMonth !== "function")  throw new Test262Error('#71: typeof Date.prototype.setMonth === "function". Actual: ' + (typeof Date.prototype.setMonth ));
if (typeof Date.prototype['setMonth'] !== "function")  throw new Test262Error('#72: typeof Date.prototype["setMonth"] === "function". Actual: ' + (typeof Date.prototype["setMonth"] ));
if (typeof Date.prototype.setUTCMonth !== "function")  throw new Test262Error('#73: typeof Date.prototype.setUTCMonth === "function". Actual: ' + (typeof Date.prototype.setUTCMonth ));
if (typeof Date.prototype['setUTCMonth'] !== "function")  throw new Test262Error('#74: typeof Date.prototype["setUTCMonth"] === "function". Actual: ' + (typeof Date.prototype["setUTCMonth"] ));
if (typeof Date.prototype.setFullYear !== "function")  throw new Test262Error('#75: typeof Date.prototype.setFullYear === "function". Actual: ' + (typeof Date.prototype.setFullYear ));
if (typeof Date.prototype['setFullYear'] !== "function")  throw new Test262Error('#76: typeof Date.prototype["setFullYear"] === "function". Actual: ' + (typeof Date.prototype["setFullYear"] ));
if (typeof Date.prototype.setUTCFullYear !== "function")  throw new Test262Error('#77: typeof Date.prototype.setUTCFullYear === "function". Actual: ' + (typeof Date.prototype.setUTCFullYear ));
if (typeof Date.prototype['setUTCFullYear'] !== "function")  throw new Test262Error('#78: typeof Date.prototype["setUTCFullYear"] === "function". Actual: ' + (typeof Date.prototype["setUTCFullYear"] ));
if (typeof Date.prototype.toLocaleString !== "function")  throw new Test262Error('#81: typeof Date.prototype.toLocaleString === "function". Actual: ' + (typeof Date.prototype.toLocaleString ));
if (typeof Date.prototype['toLocaleString'] !== "function")  throw new Test262Error('#82: typeof Date.prototype["toLocaleString"] === "function". Actual: ' + (typeof Date.prototype["toLocaleString"] ));
if (typeof Date.prototype.toUTCString !== "function")  throw new Test262Error('#83: typeof Date.prototype.toUTCString === "function". Actual: ' + (typeof Date.prototype.toUTCString ));
if (typeof Date.prototype['toUTCString'] !== "function")  throw new Test262Error('#84: typeof Date.prototype["toUTCString"] === "function". Actual: ' + (typeof Date.prototype["toUTCString"] ));
