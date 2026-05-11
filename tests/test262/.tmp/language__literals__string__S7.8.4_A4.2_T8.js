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
info: "CharacterEscapeSequnce :: NonEscapeSequence"
es5id: 7.8.4_A4.2_T8
description: "NonEscapeSequence :: RUSSIAN SMALL ALPHABET"
---*/

//CHECK#а-я

if ("а" !== "\а") {
  throw new Test262Error('#а');
}

if ("б" !== "\б") {
  throw new Test262Error('#б');
}

if ("в" !== "\в") {
  throw new Test262Error('#в');
}

if ("г" !== "\г") {
  throw new Test262Error('#г');
}

if ("д" !== "\д") {
  throw new Test262Error('#д');
}

if ("е" !== "\е") {
  throw new Test262Error('#е');
}

if ("ж" !== "\ж") {
  throw new Test262Error('#ж');
}

if ("з" !== "\з") {
  throw new Test262Error('#з');
}

if ("и" !== "\и") {
  throw new Test262Error('#и');
}

if ("й" !== "\й") {
  throw new Test262Error('#й');
}

if ("к" !== "\к") {
  throw new Test262Error('#к');
}

if ("л" !== "\л") {
  throw new Test262Error('#л');
}

if ("м" !== "\м") {
  throw new Test262Error('#м');
}

if ("н" !== "\н") {
  throw new Test262Error('#н');
}

if ("о" !== "\о") {
  throw new Test262Error('#о');
}

if ("п" !== "\п") {
  throw new Test262Error('#п');
}

if ("р" !== "\р") {
  throw new Test262Error('#р');
}

if ("с" !== "\с") {
  throw new Test262Error('#с');
}

if ("т" !== "\т") {
  throw new Test262Error('#т');
}

if ("у" !== "\у") {
  throw new Test262Error('#у');
}

if ("ф" !== "\ф") {
  throw new Test262Error('#ф');
}

if ("х" !== "\х") {
  throw new Test262Error('#х');
}

if ("ц" !== "\ц") {
  throw new Test262Error('#ц');
}

if ("ч" !== "\ч") {
  throw new Test262Error('#ч');
}

if ("ш" !== "\ш") {
  throw new Test262Error('#ш');
}

if ("щ" !== "\щ") {
  throw new Test262Error('#щ');
}

if ("ъ" !== "\ъ") {
  throw new Test262Error('#ъ');
}

if ("ы" !== "\ы") {
  throw new Test262Error('#ы');
}

if ("ь" !== "\ь") {
  throw new Test262Error('#ь');
}

if ("э" !== "\э") {
  throw new Test262Error('#э');
}

if ("ю" !== "\ю") {
  throw new Test262Error('#ю');
}

if ("я" !== "\я") {
  throw new Test262Error('#я');
}

if ("ё" !== "\ё") {
  throw new Test262Error('#ё');
}
