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


// This file was procedurally generated from the following sources:
// - src/async-generators/yield-star-sync-return.case
// - src/async-generators/default/async-declaration.template
/*---
description: execution order for yield* with sync iterator and return() (Async generator Function declaration)
esid: prod-AsyncGeneratorDeclaration
features: [Symbol.iterator, async-iteration, Symbol.asyncIterator]
flags: [generated, async]
info: |
    Async Generator Function Definitions

    AsyncGeneratorDeclaration:
      async [no LineTerminator here] function * BindingIdentifier ( FormalParameters ) {
        AsyncGeneratorBody }


    YieldExpression: yield * AssignmentExpression

    ...
    6. Repeat
      ...
      c. Else,
        i. Assert: received.[[Type]] is return.
        ii. Let return be ? GetMethod(iterator, "return").
        iii. If return is undefined, return Completion(received).
        iv. Let innerReturnResult be ? Call(return, iterator,
            « received.[[Value]] »).
        v. If generatorKind is async, then set innerReturnResult to
           ? Await(innerReturnResult).
        ...
        vii. Let done be ? IteratorComplete(innerReturnResult).
        viii. If done is true, then
             1. Let value be ? IteratorValue(innerReturnResult).
             2. Return Completion{[[Type]]: return, [[Value]]: value,
                [[Target]]: empty}.
        ix. Let received be GeneratorYield(innerResult).

    %AsyncFromSyncIteratorPrototype%.return ( value )

    5. Let return be GetMethod(syncIterator, "return").
    ...
    ...
    8. Let returnResult be Call(return, syncIterator, « value »).
    ...
    11. Let returnValue be IteratorValue(returnResult).
    ..
    13. Let returnDone be IteratorComplete(returnResult).
    ...
    16. Perform ! Call(valueWrapperCapability.[[Resolve]], undefined, « returnValue »).
    ...
    18. Set onFulfilled.[[Done]] to returnDone.
    19. Perform ! PerformPromiseThen(valueWrapperCapability.[[Promise]],
        onFulfilled, undefined, promiseCapability).
    ...

---*/
var log = [];
var obj = {
  [Symbol.iterator]() {
    var returnCount = 0;
    return {
      name: "syncIterator",
      get next() {
        log.push({ name: "get next" });
        return function() {
          return {
            value: "next-value-1",
            done: false
          };
        };
      },
      get return() {
        log.push({
          name: "get return",
          thisValue: this
        });
        return function() {
          log.push({
            name: "call return",
            thisValue: this,
            args: [...arguments]
          });

          returnCount++;
          if (returnCount == 1) {
            return {
              name: "return-result-1",
              get value() {
                log.push({
                  name: "get return value (1)",
                  thisValue: this
                });
                return "return-value-1";
              },
              get done() {
                log.push({
                  name: "get return done (1)",
                  thisValue: this
                });
                return false;
              }
            };
          }

          return {
            name: "return-result-2",
            get value() {
              log.push({
                name: "get return value (2)",
                thisValue: this
              });
              return "return-value-2";
            },
            get done() {
              log.push({
                name: "get return done (2)",
                thisValue: this
              });
              return true;
            }
          };
        };
      }
    };
  }
};



var callCount = 0;

async function *gen() {
  callCount += 1;
  log.push({ name: "before yield*" });
    yield* obj;

}

var iter = gen();

assert.sameValue(log.length, 0, "log.length");

iter.next().then(v => {
  assert.sameValue(log[0].name, "before yield*");

  assert.sameValue(log[1].name, "get next");

  assert.sameValue(v.value, "next-value-1");
  assert.sameValue(v.done, false);

  assert.sameValue(log.length, 2, "log.length");

  iter.return("return-arg-1").then(v => {
    assert.sameValue(log[2].name, "get return");
    assert.sameValue(log[2].thisValue.name, "syncIterator", "get return thisValue");

    assert.sameValue(log[3].name, "call return");
    assert.sameValue(log[3].thisValue.name, "syncIterator", "return thisValue");
    assert.sameValue(log[3].args.length, 1, "return args.length");
    assert.sameValue(log[3].args[0], "return-arg-1", "return args[0]");

    assert.sameValue(log[4].name, "get return done (1)");
    assert.sameValue(log[4].thisValue.name, "return-result-1", "get return done thisValue");

    assert.sameValue(log[5].name, "get return value (1)");
    assert.sameValue(log[5].thisValue.name, "return-result-1", "get return value thisValue");

    assert.sameValue(v.value, "return-value-1");
    assert.sameValue(v.done, false);

    assert.sameValue(log.length, 6, "log.length");

    iter.return().then(v => {
      assert.sameValue(log[6].name, "get return");
      assert.sameValue(log[6].thisValue.name, "syncIterator", "get return thisValue");

      assert.sameValue(log[7].name, "call return");
      assert.sameValue(log[7].thisValue.name, "syncIterator", "get return thisValue");
      assert.sameValue(log[7].args.length, 1, "return args.length");
      assert.sameValue(log[7].args[0], undefined, "return args[0]");

      assert.sameValue(log[8].name, "get return done (2)");
      assert.sameValue(log[8].thisValue.name, "return-result-2", "get return done thisValue");

      assert.sameValue(log[9].name, "get return value (2)");
      assert.sameValue(log[9].thisValue.name, "return-result-2", "get return value thisValue");

      assert.sameValue(v.value, "return-value-2");
      assert.sameValue(v.done, true);

      assert.sameValue(log.length, 10, "log.length");
    }).then($DONE, $DONE);
  }).catch($DONE);
}).catch($DONE);

assert.sameValue(callCount, 1);
