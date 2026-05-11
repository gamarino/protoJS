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
// - src/async-generators/yield-star-async-next.case
// - src/async-generators/default/async-expression-named.template
/*---
description: Execution order for yield* with async iterator and next() (Named async generator expression)
esid: prod-AsyncGeneratorExpression
features: [Symbol.iterator, async-iteration, Symbol.asyncIterator]
flags: [generated, async]
info: |
    Async Generator Function Definitions

    AsyncGeneratorExpression :
      async [no LineTerminator here] function * BindingIdentifier ( FormalParameters ) {
        AsyncGeneratorBody }


    
---*/
var log = [];
var obj = {
  get [Symbol.iterator]() {
    log.push({ name: "get [Symbol.iterator]" });
  },
  get [Symbol.asyncIterator]() {
    log.push({
      name: "get [Symbol.asyncIterator]",
      thisValue: this
    });
    return function() {
      log.push({
        name: "call [Symbol.asyncIterator]",
        thisValue: this,
        args: [...arguments]
      });
      var nextCount = 0;
      return {
        name: "asyncIterator",
        get next() {
          log.push({
            name: "get next",
            thisValue: this
          });
          return function() {
            log.push({
              name: "call next",
              thisValue: this,
              args: [...arguments]
            });

            nextCount++;
            if (nextCount == 1) {
              return {
                name: "next-promise-1",
                get then() {
                  log.push({
                    name: "get next then (1)",
                    thisValue: this
                  });
                  return function(resolve) {
                    log.push({
                      name: "call next then (1)",
                      thisValue: this,
                      args: [...arguments]
                    });

                    resolve({
                      name: "next-result-1",
                      get value() {
                        log.push({
                          name: "get next value (1)",
                          thisValue: this
                        });
                        return "next-value-1";
                      },
                      get done() {
                        log.push({
                          name: "get next done (1)",
                          thisValue: this
                        });
                        return false;
                      }
                    });
                  };
                }
              };
            }

            return {
              name: "next-promise-2",
              get then() {
                log.push({
                  name: "get next then (2)",
                  thisValue: this
                });
                return function(resolve) {
                  log.push({
                    name: "call next then (2)",
                    thisValue: this,
                    args: [...arguments]
                  });

                  resolve({
                    name: "next-result-2",
                    get value() {
                      log.push({
                        name: "get next value (2)",
                        thisValue: this
                      });
                      return "next-value-2";
                    },
                    get done() {
                      log.push({
                        name: "get next done (2)",
                        thisValue: this
                      });
                      return true;
                    }
                  });
                };
              }
            };
          };
        }
      };
    };
  }
};



var callCount = 0;

var gen = async function *g() {
  callCount += 1;
  log.push({ name: "before yield*" });
    var v = yield* obj;
    log.push({
      name: "after yield*",
      value: v
    });
    return "return-value";

};

var iter = gen();

assert.sameValue(log.length, 0, "log.length");

iter.next("next-arg-1").then(v => {
  assert.sameValue(log[0].name, "before yield*");

  assert.sameValue(log[1].name, "get [Symbol.asyncIterator]");
  assert.sameValue(log[1].thisValue, obj, "get [Symbol.asyncIterator] thisValue");

  assert.sameValue(log[2].name, "call [Symbol.asyncIterator]");
  assert.sameValue(log[2].thisValue, obj, "[Symbol.asyncIterator] thisValue");
  assert.sameValue(log[2].args.length, 0, "[Symbol.asyncIterator] args.length");

  assert.sameValue(log[3].name, "get next");
  assert.sameValue(log[3].thisValue.name, "asyncIterator", "get next thisValue");

  assert.sameValue(log[4].name, "call next");
  assert.sameValue(log[4].thisValue.name, "asyncIterator", "next thisValue");
  assert.sameValue(log[4].args.length, 1, "next args.length");
  assert.sameValue(log[4].args[0], undefined, "next args[0]");

  assert.sameValue(log[5].name, "get next then (1)");
  assert.sameValue(log[5].thisValue.name, "next-promise-1", "get next then thisValue");

  assert.sameValue(log[6].name, "call next then (1)");
  assert.sameValue(log[6].thisValue.name, "next-promise-1", "next then thisValue");
  assert.sameValue(log[6].args.length, 2, "next then args.length");
  assert.sameValue(typeof log[6].args[0], "function", "next then args[0]");
  assert.sameValue(typeof log[6].args[1], "function", "next then args[1]");

  assert.sameValue(log[7].name, "get next done (1)");
  assert.sameValue(log[7].thisValue.name, "next-result-1", "get next done thisValue");

  assert.sameValue(log[8].name, "get next value (1)");
  assert.sameValue(log[8].thisValue.name, "next-result-1", "get next value thisValue");

  assert.sameValue(v.value, "next-value-1");
  assert.sameValue(v.done, false);

  assert.sameValue(log.length, 9, "log.length");

  iter.next("next-arg-2").then(v => {
    assert.sameValue(log[9].name, "call next");
    assert.sameValue(log[9].thisValue.name, "asyncIterator", "next thisValue");
    assert.sameValue(log[9].args.length, 1, "next args.length");
    assert.sameValue(log[9].args[0], "next-arg-2", "next args[0]");

    assert.sameValue(log[10].name, "get next then (2)");
    assert.sameValue(log[10].thisValue.name, "next-promise-2", "get next then thisValue");

    assert.sameValue(log[11].name, "call next then (2)");
    assert.sameValue(log[11].thisValue.name, "next-promise-2", "next then thisValue");
    assert.sameValue(log[11].args.length, 2, "next then args.length");
    assert.sameValue(typeof log[11].args[0], "function", "next then args[0]");
    assert.sameValue(typeof log[11].args[1], "function", "next then args[1]");

    assert.sameValue(log[12].name, "get next done (2)");
    assert.sameValue(log[12].thisValue.name, "next-result-2", "get next done thisValue");

    assert.sameValue(log[13].name, "get next value (2)");
    assert.sameValue(log[13].thisValue.name, "next-result-2", "get next value thisValue");

    assert.sameValue(log[14].name, "after yield*");
    assert.sameValue(log[14].value, "next-value-2");

    assert.sameValue(v.value, "return-value");
    assert.sameValue(v.done, true);

    assert.sameValue(log.length, 15, "log.length");
  }).then($DONE, $DONE);
}).catch($DONE);

assert.sameValue(callCount, 1);
