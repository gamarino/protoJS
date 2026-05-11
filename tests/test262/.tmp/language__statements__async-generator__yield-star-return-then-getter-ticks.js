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


// Copyright (C) 2019 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-generator-function-definitions-runtime-semantics-evaluation
description: >
  Return resumption value is awaited upon and hence is treated as a thenable.
info: |
  15.5.5 Runtime Semantics: Evaluation
    YieldExpression : yield* AssignmentExpression

    ...
    7. Repeat,
      a. If received.[[Type]] is normal, then
        i. Let innerResult be ? Call(iteratorRecord.[[NextMethod]], iteratorRecord.[[Iterator]],
                                     « received.[[Value]] »).
        ii. If generatorKind is async, then set innerResult to ? Await(innerResult).
        ...
        vi. If generatorKind is async, then set received to AsyncGeneratorYield(? IteratorValue(innerResult)).
        ...
      ...
      c. Else,
        i. Assert: received.[[Type]] is return.
        ii. Let return be ? GetMethod(iterator, "return").
        iii. If return is undefined, then
          1. If generatorKind is async, then set received.[[Value]] to ? Await(received.[[Value]]).
          2. Return Completion(received).
        ...

  27.6.3.8 AsyncGeneratorYield ( value )
    ...
    12. If queue is not empty, then
      ...
    13. Else,
      ...
      c. Set the code evaluation state of genContext such that when evaluation is resumed with a
       Completion resumptionValue the following steps will be performed:
        i. Return ? AsyncGeneratorUnwrapYieldResumption(resumptionValue).

  27.6.3.7 AsyncGeneratorUnwrapYieldResumption ( resumptionValue  )
    ...
    2. Let awaited be Completion(Await(resumptionValue.[[Value]])).
    ...

  6.2.3.1 Await
    ...
    2. Let promise be ? PromiseResolve(%Promise%, « value »).
    ...

  27.2.4.7.1 PromiseResolve ( C, x )
    ...
    2. Let promiseCapability be ? NewPromiseCapability(C).
    3. Perform ? Call(promiseCapability.[[Resolve]], undefined, « x »).
    ...

  27.2.1.5 NewPromiseCapability ( C )
    ...
    7. Let promise be ? Construct(C, « executor »).
    ...

  27.2.3.1 Promise ( executor )
    ...
    8. Let resolvingFunctions be CreateResolvingFunctions(promise).
    ...

  27.2.1.3 CreateResolvingFunctions ( promise )
    ...
    2. Let stepsResolve be the algorithm steps defined in Promise Resolve Functions (25.6.1.3.2).
    3. Let resolve be CreateBuiltinFunction(stepsResolve, « [[Promise]], [[AlreadyResolved]] »).
    ...

  27.2.1.3.2 Promise Resolve Functions
    ...
    9. Let then be Get(resolution, "then").
    ...

includes: [compareArray.js]
flags: [async]
features: [async-iteration]
---*/

var expected = [
  "start",

  // `Await(innerResult)` promise resolved.
  "tick 1",

  // "then" of `resumptionValue.[[Value]]` accessed.
  "get then",

  // `Await(resumptionValue.[[Value]])` promise resolved.
  "tick 2",

  // Get iterator "return" method.
  "get return",

  // "then" of `received.[[Value]]` accessed.
  "get then",

  // `Await(received.[[Value]])` promise resolved.
  "tick 3",
];

var actual = [];

var asyncIter = {
  [Symbol.asyncIterator]() {
    return this;
  },
  next() {
    return {
      done: false,
    };
  },
  get return() {
    actual.push("get return");
  }
};

async function* f() {
  actual.push("start");
  yield* asyncIter;
  actual.push("stop - never reached");
}

Promise.resolve(0)
  .then(() => actual.push("tick 1"))
  .then(() => actual.push("tick 2"))
  .then(() => actual.push("tick 3"))
  .then(() => {
    assert.compareArray(actual, expected, "Ticks for return with thenable getter");
}).then($DONE, $DONE);

var it = f();

// Start generator execution.
it.next();

// Stop generator execution.
it.return({
  get then() {
    actual.push("get then");
  }
});
