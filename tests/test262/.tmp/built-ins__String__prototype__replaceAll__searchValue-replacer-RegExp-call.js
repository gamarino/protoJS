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


// Copyright (C) 2019 Leo Balter. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-string.prototype.replaceall
description: >
  A RegExp searchValue's Symbol.replace can be called instead of the next steps of replaceAll
info: |
  String.prototype.replaceAll ( searchValue, replaceValue )

  1. Let O be RequireObjectCoercible(this value).
  2. If searchValue is neither undefined nor null, then
    a. Let isRegExp be ? IsRegExp(searchString).
    b. If isRegExp is true, then
      i. Let flags be ? Get(searchValue, "flags").
      ii. Perform ? RequireObjectCoercible(flags).
      iii. If ? ToString(flags) does not contain "g", throw a TypeError exception.
    c. Let replacer be ? GetMethod(searchValue, @@replace).
    d. If replacer is not undefined, then
      i. Return ? Call(replacer, searchValue, « O, replaceValue »).
  3. Let string be ? ToString(O).
  4. Let searchString be ? ToString(searchValue).
  ...
features: [String.prototype.replaceAll, Symbol.replace, class]
---*/

let called = 0;

class RE extends RegExp {
  [Symbol.replace](...args) {
    const actual = super[Symbol.replace](...args);

    // Ordering is intentional to observe call from super
    called += 1;
    return actual;
  }

  toString() {
    throw 'Should not call toString on searchValue';
  }
}

const samples = [
  [ ['b', 'g'], 'abc abc abc', 'z', 'azc azc azc' ],
  [ ['b', 'gy'], 'abc abc abc', 'z', 'abc abc abc' ],
  [ ['b', 'giy'], 'abc abc abc', 'z', 'abc abc abc' ],
  [ [ '[A-Z]', 'g' ], 'No Uppercase!', '', 'o ppercase!' ],
  [ [ '[A-Z]', 'gy' ], 'No Uppercase?', '', 'o Uppercase?' ],
  [ [ '[A-Z]', 'gy' ], 'NO UPPERCASE!', '', ' UPPERCASE!' ],
  [ [ 'a(b)(ca)', 'g' ], 'abcabcabcabc', '$2-$1', 'ca-bbcca-bbc' ],
  [ [ '(a(.))', 'g' ], 'abcabcabcabc', '$1$2$3', 'abb$3cabb$3cabb$3cabb$3c' ],
  [ [ '(((((((((((((a(.).).).).).).).).))))))', 'g' ], 'aabacadaeafagahaiajakalamano a azaya', '($10)-($12)-($1)', '(aabaca)-(aaba)-(aabacadaea)f(agahai)-(agah)-(agahaiajak)(alaman)-(alam)-(alamano a )azaya' ],
  [ [ 'b', 'g' ], 'abcba', '$\'', 'acbacaa' ],
  [ [ 'b', 'g' ], 'abcba', '$`', 'aacabca' ],
  [ [ '(?<named>b)', 'g' ], 'abcba', '($<named>)', 'a(b)c(b)a' ],
  [ [ '(?<named>b)', 'g' ], 'abcba', '($<named)', 'a($<named)c($<named)a' ],
  [ [ '(?<named>b)', 'g' ], 'abcba', '($<unnamed>)', 'a()c()a' ],
  [ [ 'a(b)(ca)', 'g' ], 'abcabcabcabc', '($$)', '($)bc($)bc' ],
  [ [ 'a(b)(ca)', 'g' ], 'abcabcabcabc', '($)', '($)bc($)bc' ],
  [ [ 'a(b)(ca)', 'g' ], 'abcabcabcabc', '($$$$)', '($$)bc($$)bc' ],
  [ [ 'a(b)(ca)', 'g' ], 'abcabcabcabc', '($$$)', '($$)bc($$)bc' ],
  [ [ 'a(b)(ca)', 'g' ], 'abcabcabcabc', '($$&)', '($&)bc($&)bc' ],
  [ [ 'a(b)(ca)', 'g' ], 'abcabcabcabc', '($$1)', '($1)bc($1)bc' ],
  [ [ 'a(b)(ca)', 'g' ], 'abcabcabcabc', '($$`)', '($`)bc($`)bc' ],
  [ [ 'a(b)(ca)', 'g' ], 'abcabcabcabc', '($$\')', '($\')bc($\')bc' ],
  [ [ 'a(?<z>b)(ca)', 'g' ], 'abcabcabcabc', '($$<z>)', '($<z>)bc($<z>)bc' ],
  [ [ 'a(b)(ca)', 'g' ], 'abcabcabcabc', '($&)', '(abca)bc(abca)bc' ],
];

let count = 0;
for (const [ [ reStr, flags ], thisValue, replaceValue, expected ] of samples) {
  const searchValue = new RE(reStr, flags);

  called = 0;
  const actual = thisValue.replaceAll(searchValue, replaceValue);

  const message = `sample ${count}: '${thisValue}'.replaceAll(/${reStr}/${flags}, '${replaceValue}')`;

  assert.sameValue(called, 1, message);
  assert.sameValue(actual, expected, message);
  count += 1;
}
