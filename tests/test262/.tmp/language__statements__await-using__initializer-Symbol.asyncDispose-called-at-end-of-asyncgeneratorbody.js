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


// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    A collection of assertion and wrapper functions for testing asynchronous built-ins.
defines: [asyncTest, assert.throwsAsync]
---*/

/**
 * Defines the **sole** asynchronous test of a file.
 * @see {@link ../docs/rfcs/async-helpers.md} for background.
 *
 * @param {Function} testFunc a callback whose returned promise indicates test results
 *   (fulfillment for success, rejection for failure)
 * @returns {void}
 */
function asyncTest(testFunc) {
  if (!Object.prototype.hasOwnProperty.call(globalThis, "$DONE")) {
    throw new Test262Error("asyncTest called without async flag");
  }
  if (typeof testFunc !== "function") {
    $DONE(new Test262Error("asyncTest called with non-function argument"));
    return;
  }
  try {
    testFunc().then(
      function () {
        $DONE();
      },
      function (error) {
        $DONE(error);
      }
    );
  } catch (syncError) {
    $DONE(syncError);
  }
}

/**
 * Asserts that a callback asynchronously throws an instance of a particular
 * error (i.e., returns a promise whose rejection value is an object referencing
 * the constructor).
 *
 * @param {Function} expectedErrorConstructor the expected constructor of the
 *   rejection value
 * @param {Function} func the callback
 * @param {string} [message] the prefix to use for failure messages
 * @returns {Promise<void>} fulfills if the expected error is thrown,
 *   otherwise rejects
 */
assert.throwsAsync = function (expectedErrorConstructor, func, message) {
  return new Promise(function (resolve) {
    var fail = function (detail) {
      if (message === undefined) {
        throw new Test262Error(detail);
      }
      throw new Test262Error(message + " " + detail);
    };
    if (typeof expectedErrorConstructor !== "function") {
      fail("assert.throwsAsync called with an argument that is not an error constructor");
    }
    if (typeof func !== "function") {
      fail("assert.throwsAsync called with an argument that is not a function");
    }
    var expectedName = expectedErrorConstructor.name;
    var expectation = "Expected a " + expectedName + " to be thrown asynchronously";
    var res;
    try {
      res = func();
    } catch (thrown) {
      fail(expectation + " but the function threw synchronously");
    }
    if (res === null || typeof res !== "object" || typeof res.then !== "function") {
      fail(expectation + " but result was not a thenable");
    }
    var onResFulfilled, onResRejected;
    var resSettlementP = new Promise(function (onFulfilled, onRejected) {
      onResFulfilled = onFulfilled;
      onResRejected = onRejected;
    });
    try {
      res.then(onResFulfilled, onResRejected)
    } catch (thrown) {
      fail(expectation + " but .then threw synchronously");
    }
    resolve(resSettlementP.then(
      function () {
        fail(expectation + " but no exception was thrown at all");
      },
      function (thrown) {
        var actualName;
        if (thrown === null || typeof thrown !== "object") {
          fail(expectation + " but thrown value was not an object");
        } else if (thrown.constructor !== expectedErrorConstructor) {
          actualName = thrown.constructor.name;
          if (expectedName === actualName) {
            fail(expectation +
              " but got a different error constructor with the same name");
          }
          fail(expectation + " but got a " + actualName);
        }
      }
    ));
  });
};


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


// Copyright (C) 2023 Ron Buckton. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-asyncgeneratorstart
description: Initialized value is disposed at end of AsyncGeneratorBody
info: |
  AsyncGeneratorStart ( generator, generatorBody )

  1. Assert: generator.[[AsyncGeneratorState]] is undefined.
  2. Let genContext be the running execution context.
  3. Set the Generator component of genContext to generator.
  4. Let closure be a new Abstract Closure with no parameters that captures generatorBody and performs the following steps when called:
    a. Let acGenContext be the running execution context.
    b. Let acGenerator be the Generator component of acGenContext.
    c. If generatorBody is a Parse Node, then
      i. Let result be Completion(Evaluation of generatorBody).
    d. Else,
      i. Assert: generatorBody is an Abstract Closure with no parameters.
      ii. Let result be Completion(generatorBody()).
    e. Assert: If we return here, the async generator either threw an exception or performed either an implicit or explicit return.
    f. Remove acGenContext from the execution context stack and restore the execution context that is at the top of the execution context stack as the running execution context.
    g. Set acGenerator.[[AsyncGeneratorState]] to completed.
    h. Let env be genContext's LexicalEnvironment.
    i. If env is not undefined, then
      i. Assert: env is a Declarative Environment Record
      ii. Set result to DisposeResources(env.[[DisposeCapability]], result).
    h. If result.[[Type]] is normal, set result to NormalCompletion(undefined).
    i. If result.[[Type]] is return, set result to NormalCompletion(result.[[Value]]).
    j. Perform AsyncGeneratorCompleteStep(acGenerator, result, true).
    k. Perform AsyncGeneratorDrainQueue(acGenerator).
    l. Return undefined.
  5. Set the code evaluation state of genContext such that when evaluation is resumed for that execution context, closure will be called with no arguments.
  6. Set generator.[[AsyncGeneratorContext]] to genContext.
  7. Set generator.[[AsyncGeneratorState]] to suspendedStart.
  8. Set generator.[[AsyncGeneratorQueue]] to a new empty List.
  9. Return unused.

  DisposeResources ( disposeCapability, completion )

  1. For each resource of disposeCapability.[[DisposableResourceStack]], in reverse list order, do
    a. Let result be Dispose(resource.[[ResourceValue]], resource.[[Hint]], resource.[[DisposeMethod]]).
    b. If result.[[Type]] is throw, then
      i. If completion.[[Type]] is throw, then
        1. Set result to result.[[Value]].
        2. Let suppressed be completion.[[Value]].
        3. Let error be a newly created SuppressedError object.
        4. Perform ! CreateNonEnumerableDataPropertyOrThrow(error, "error", result).
        5. Perform ! CreateNonEnumerableDataPropertyOrThrow(error, "suppressed", suppressed).
        6. Set completion to ThrowCompletion(error).
      ii. Else,
        1. Set completion to result.
  2. Return completion.

  Dispose ( V, hint, method )

  1. If method is undefined, let result be undefined.
  2. Else, let result be ? Call(method, V).
  3. If hint is async-dispose, then
    a. Perform ? Await(result).
  4. Return undefined.

flags: [async]
includes: [asyncHelpers.js]
features: [explicit-resource-management]
---*/

asyncTest(async function () {

  var resource = {
      disposed: false,
      async [Symbol.asyncDispose]() {
          this.disposed = true;
      }
  };

  var releaseF;
  var suspendFPromise = new Promise(function (resolve) { releaseF = resolve; });

  async function * f() {
      await using _ = resource;
      yield;
      await suspendFPromise;
  }

  var g = f();

  var wasDisposedBeforeAsyncGeneratorStarted = resource.disposed;

  await g.next();

  var wasDisposedWhileSuspendedForYield = resource.disposed;

  var nextPromise = g.next();

  var wasDisposedWhileSuspendedForAwait = resource.disposed;

  releaseF();
  await nextPromise;

  var isDisposedAfterCompleted = resource.disposed;

  assert.sameValue(wasDisposedBeforeAsyncGeneratorStarted, false, 'Expected resource to not have been disposed prior to async generator start');
  assert.sameValue(wasDisposedWhileSuspendedForYield, false, 'Expected resource to not have been disposed while async generator function is suspended for yield');
  assert.sameValue(wasDisposedWhileSuspendedForAwait, false, 'Expected resource to not have been disposed while async generator function is suspended during await');
  assert.sameValue(isDisposedAfterCompleted, true, 'Expected resource to have been disposed after async generator function completed');
});
