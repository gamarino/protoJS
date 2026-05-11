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


// Copyright 2024 Mathias Bynens. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Mathias Bynens
esid: sec-names-and-keywords
description: |
  Test that Unicode v8.0.0 ID_Continue characters are accepted as
  identifier part characters in escaped form, i.e.
  - \uXXXX or \u{XXXX} for BMP symbols
  - \u{XXXXXX} for astral symbols
info: |
  Generated by https://github.com/mathiasbynens/caniunicode
---*/

var _\u08E3\uA69E\uFE2E\uFE2F\u{111CA}\u{111CB}\u{111CC}\u{11300}\u{115DC}\u{115DD}\u{1171D}\u{1171E}\u{1171F}\u{11720}\u{11721}\u{11722}\u{11723}\u{11724}\u{11725}\u{11726}\u{11727}\u{11728}\u{11729}\u{1172A}\u{1172B}\u{11730}\u{11731}\u{11732}\u{11733}\u{11734}\u{11735}\u{11736}\u{11737}\u{11738}\u{11739}\u{1DA00}\u{1DA01}\u{1DA02}\u{1DA03}\u{1DA04}\u{1DA05}\u{1DA06}\u{1DA07}\u{1DA08}\u{1DA09}\u{1DA0A}\u{1DA0B}\u{1DA0C}\u{1DA0D}\u{1DA0E}\u{1DA0F}\u{1DA10}\u{1DA11}\u{1DA12}\u{1DA13}\u{1DA14}\u{1DA15}\u{1DA16}\u{1DA17}\u{1DA18}\u{1DA19}\u{1DA1A}\u{1DA1B}\u{1DA1C}\u{1DA1D}\u{1DA1E}\u{1DA1F}\u{1DA20}\u{1DA21}\u{1DA22}\u{1DA23}\u{1DA24}\u{1DA25}\u{1DA26}\u{1DA27}\u{1DA28}\u{1DA29}\u{1DA2A}\u{1DA2B}\u{1DA2C}\u{1DA2D}\u{1DA2E}\u{1DA2F}\u{1DA30}\u{1DA31}\u{1DA32}\u{1DA33}\u{1DA34}\u{1DA35}\u{1DA36}\u{1DA3B}\u{1DA3C}\u{1DA3D}\u{1DA3E}\u{1DA3F}\u{1DA40}\u{1DA41}\u{1DA42}\u{1DA43}\u{1DA44}\u{1DA45}\u{1DA46}\u{1DA47}\u{1DA48}\u{1DA49}\u{1DA4A}\u{1DA4B}\u{1DA4C}\u{1DA4D}\u{1DA4E}\u{1DA4F}\u{1DA50}\u{1DA51}\u{1DA52}\u{1DA53}\u{1DA54}\u{1DA55}\u{1DA56}\u{1DA57}\u{1DA58}\u{1DA59}\u{1DA5A}\u{1DA5B}\u{1DA5C}\u{1DA5D}\u{1DA5E}\u{1DA5F}\u{1DA60}\u{1DA61}\u{1DA62}\u{1DA63}\u{1DA64}\u{1DA65}\u{1DA66}\u{1DA67}\u{1DA68}\u{1DA69}\u{1DA6A}\u{1DA6B}\u{1DA6C}\u{1DA75}\u{1DA84}\u{1DA9B}\u{1DA9C}\u{1DA9D}\u{1DA9E}\u{1DA9F}\u{1DAA1}\u{1DAA2}\u{1DAA3}\u{1DAA4}\u{1DAA5}\u{1DAA6}\u{1DAA7}\u{1DAA8}\u{1DAA9}\u{1DAAA}\u{1DAAB}\u{1DAAC}\u{1DAAD}\u{1DAAE}\u{1DAAF};
