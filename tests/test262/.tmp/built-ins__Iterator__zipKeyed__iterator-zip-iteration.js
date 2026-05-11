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
    Deprecated now that compareArray is defined in assert.js.
defines: [compareArray]
---*/


// Copyright (C) 2025 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-iterator.zipkeyed
description: >
  Perform iteration in IteratorZip.
info: |
  Iterator.zipKeyed ( iterables [ , options ] )
    ...
    16. Return IteratorZip(iters, mode, padding, finishResults).

  IteratorZip ( iters, mode, padding, finishResults )
    3. Let closure be a new Abstract Closure with no parameters that captures
       iters, iterCount, openIters, mode, padding, and finishResults, and
       performs the following steps when called:
      ...
      b. Repeat,
        ...
        iii. For each integer i such that 0 ≤ i < iterCount, in ascending order, do
          ...
          3. Else,
            a. Let result be Completion(IteratorStepValue(iter)).
          ...
includes: [compareArray.js]
features: [joint-iteration]
---*/

var modes = [
  "shortest",
  "longest",
  "strict",
];

function makeIterator(log, name, elements) {
  var elementsIter = elements.values();
  var iterator = {
    next() {
      log.push(`call ${name} next`);

      // Called with the correct receiver and no arguments.
      assert.sameValue(this, iterator);
      assert.sameValue(arguments.length, 0);

      var result = elementsIter.next();
      return {
        get done() {
          log.push(`get ${name}.result.done`);
          return result.done;
        },
        get value() {
          log.push(`get ${name}.result.value`);
          return result.value;
        },
      };
    },
    return() {
      log.push(`call ${name} return`);

      // Called with the correct receiver and no arguments.
      assert.sameValue(this, iterator);
      assert.sameValue(arguments.length, 0);

      return {
        get done() {
          log.push(`unexpected get ${name}.result.done`);
          return result.done;
        },
        get value() {
          log.push(`unexpected get ${name}.result.value`);
          return result.value;
        },
      };
    }
  };
  return iterator;
}

for (var mode of modes) {
  var log = [];
  var iterables = {
    first: makeIterator(log, "first", [1, 2, 3]),
    second: makeIterator(log, "second", [4, 5, 6]),
    third: makeIterator(log, "third", [7, 8, 9]),
  };
  var it = Iterator.zipKeyed(iterables, {mode});

  log.push("start");
  for (var v of it) {
    log.push("loop");
  }

  var expected = [
    "start",

    "call first next",
      "get first.result.done",
      "get first.result.value",
    "call second next",
      "get second.result.done",
      "get second.result.value",
    "call third next",
      "get third.result.done",
      "get third.result.value",
    "loop",

    "call first next",
      "get first.result.done",
      "get first.result.value",
    "call second next",
      "get second.result.done",
      "get second.result.value",
    "call third next",
      "get third.result.done",
      "get third.result.value",
    "loop",

    "call first next",
      "get first.result.done",
      "get first.result.value",
    "call second next",
      "get second.result.done",
      "get second.result.value",
    "call third next",
      "get third.result.done",
      "get third.result.value",
    "loop",
  ];

  switch (mode) {
    case "shortest": {
      expected.push(
        "call first next",
          "get first.result.done",
        "call third return",
        "call second return",
      );
      break;
    }
    case "longest":
    case "strict": {
      expected.push(
        "call first next",
          "get first.result.done",
        "call second next",
          "get second.result.done",
        "call third next",
          "get third.result.done",
      );
      break;
    }
  }

  assert.compareArray(log, expected);
}
