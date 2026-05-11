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


// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Check that an array contains a numeric sequence starting at 1
    and incrementing by 1 for each entry in the array. Used by
    Promise tests to assert the order of execution in deep Promise
    resolution pipelines.
defines: [checkSequence, checkSettledPromises]
---*/

function checkSequence(arr, message) {
  arr.forEach(function(e, i) {
    if (e !== (i+1)) {
      throw new Test262Error((message ? message : "Steps in unexpected sequence:") +
             " '" + arr.join(',') + "'");
    }
  });

  return true;
}

function checkSettledPromises(settleds, expected, message) {
  const prefix = message ? `${message}: ` : '';

  assert.sameValue(Array.isArray(settleds), true, `${prefix}Settled values is an array`);

  assert.sameValue(
    settleds.length,
    expected.length,
    `${prefix}The settled values has a different length than expected`
  );

  settleds.forEach((settled, i) => {
    assert.sameValue(
      Object.prototype.hasOwnProperty.call(settled, 'status'),
      true,
      `${prefix}The settled value has a property status`
    );

    assert.sameValue(settled.status, expected[i].status, `${prefix}status for item ${i}`);

    if (settled.status === 'fulfilled') {
      assert.sameValue(
        Object.prototype.hasOwnProperty.call(settled, 'value'),
        true,
        `${prefix}The fulfilled promise has a property named value`
      );

      assert.sameValue(
        Object.prototype.hasOwnProperty.call(settled, 'reason'),
        false,
        `${prefix}The fulfilled promise has no property named reason`
      );

      assert.sameValue(settled.value, expected[i].value, `${prefix}value for item ${i}`);
    } else {
      assert.sameValue(settled.status, 'rejected', `${prefix}Valid statuses are only fulfilled or rejected`);

      assert.sameValue(
        Object.prototype.hasOwnProperty.call(settled, 'value'),
        false,
        `${prefix}The fulfilled promise has no property named value`
      );

      assert.sameValue(
        Object.prototype.hasOwnProperty.call(settled, 'reason'),
        true,
        `${prefix}The fulfilled promise has a property named reason`
      );

      assert.sameValue(settled.reason, expected[i].reason, `${prefix}Reason value for item ${i}`);
    }
  });
}


// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
defines: [$DONE]
---*/

function __consolePrintHandle__(msg) {
  print(msg);
}

function $DONE(error) {
  if (error) {
    if(typeof error === 'object' && error !== null && 'name' in error) {
      __consolePrintHandle__('Test262:AsyncTestFailure:' + error.name + ': ' + error.message);
    } else {
      __consolePrintHandle__('Test262:AsyncTestFailure:Test262Error: ' + String(error));
    }
  } else {
    __consolePrintHandle__('Test262:AsyncTestComplete');
  }
}


// Copyright 2014 Cubane Canada, Inc.  All rights reserved.
// See LICENSE for details.

/*---
info: |
   Promise.resolve
es6id: S25.4.4.5
author: Sam Mikes
description: Promise.resolve delegates to foreign thenable
includes: [promiseHelper.js]
flags: [async]
---*/

var sequence = [];

var thenable = {
  then: function(onResolve, onReject) {

    sequence.push(3);
    assert.sameValue(sequence.length, 3);
    checkSequence(sequence, "thenable.then called");

    assert.sameValue(this, thenable, "thenable.then called with `thenable` as `this`");

    return onResolve('resolved');
  }
};

sequence.push(1);
assert.sameValue(sequence.length, 1);
checkSequence(sequence, "no async calls yet");

var p = Promise.resolve(thenable);

sequence.push(2);
assert.sameValue(sequence.length, 2);
checkSequence(sequence, "thenable.then queued but not yet called");

p.then(function(r) {
  sequence.push(4);
  assert.sameValue(sequence.length, 4);
  checkSequence(sequence, "all done");

  assert.sameValue(r, 'resolved');
}).then($DONE, $DONE);
