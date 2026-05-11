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


// Copyright (C) 2021 Rick Waldron. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-shadowrealm.prototype.evaluate
description: >
  ShadowRealm.prototype.evaluate wrapped function from return values share no identity.
features: [ShadowRealm]
---*/

assert.sameValue(
  typeof ShadowRealm.prototype.evaluate,
  'function',
  'This test must fail if ShadowRealm.prototype.evaluate is not a function'
);

const r = new ShadowRealm();

r.evaluate(`
function fn() { return 42; }
globalThis.arrow = x => x * 2;
globalThis.pFn = new Proxy(fn, {
    apply() {
        pFn.used = 1;
        return 39;
    }
});
async function aFn() {
    return 1;
}

function * genFn() {
    return 1;
}

fn.x = 'secrets';
arrow.x = 'secrets';
pFn.x = 'secrets';
aFn.x = 'secrets';
genFn.x = 'secrets';
`)

const wrappedOrdinary = r.evaluate('() => fn')();
assert.sameValue(typeof wrappedOrdinary, 'function', 'ordinary function wrapped');
assert.sameValue(wrappedOrdinary(), 42, 'ordinary, return');
assert.sameValue(wrappedOrdinary.x, undefined, 'ordinary, no property shared');
assert.sameValue(Object.prototype.hasOwnProperty.call(wrappedOrdinary, 'x'), false, 'ordinary, no own property shared');

const wrappedArrow = r.evaluate('() => arrow')();
assert.sameValue(typeof wrappedArrow, 'function', 'arrow function wrapped');
assert.sameValue(wrappedArrow(7), 14, 'arrow function, return');
assert.sameValue(wrappedArrow.x, undefined, 'arrow function, no property');
assert.sameValue(Object.prototype.hasOwnProperty.call(wrappedArrow, 'x'), false, 'arrow function, no own property shared');

const wrappedProxied = r.evaluate('() => pFn')();
assert.sameValue(typeof wrappedProxied, 'function', 'proxied ordinary function wrapped');
assert.sameValue(r.evaluate('pFn.used'), undefined, 'pFn not called yet');
assert.sameValue(wrappedProxied(), 39, 'return of the proxied callable');
assert.sameValue(r.evaluate('pFn.used'), 1, 'pfn called');
assert.sameValue(wrappedProxied.x, undefined, 'proxy callable, no property');
assert.sameValue(Object.prototype.hasOwnProperty.call(wrappedProxied, 'x'), false, 'proxy callable, no own property shared');

const wrappedAsync = r.evaluate('() => aFn')();
assert.sameValue(typeof wrappedAsync, 'function', 'async function wrapped');
assert.throws(TypeError, () => wrappedAsync(), 'wrapped function cannot return non callable object');
assert.sameValue(wrappedAsync.x, undefined, 'async function, no property');
assert.sameValue(Object.prototype.hasOwnProperty.call(wrappedAsync, 'x'), false, 'async function, no own property shared');

const wrappedGenerator = r.evaluate('() => genFn')();
assert.sameValue(typeof wrappedGenerator, 'function', 'gen function wrapped');
assert.throws(TypeError, () => wrappedGenerator(), 'wrapped function cannot return non callable object');
assert.sameValue(wrappedGenerator.x, undefined, 'generator, no property');
assert.sameValue(Object.prototype.hasOwnProperty.call(wrappedGenerator, 'x'), false, 'generator, no own property shared');
