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


// Copyright (C) 2020 Apple Inc. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
author: Michael Saboff
description: Exotic named group names in non-Unicode RegExps
esid: prod-GroupSpecifier
features: [regexp-named-groups]
---*/

/*
 Valid ID_Start / ID_Continue Unicode characters

 𝑓  \u{1d453}  \ud835 \udc53
 𝑜  \u{1d45c}  \ud835 \udc5c
 𝑥  \u{id465}  \ud835 \udc65

 𝓓  \u{1d4d3}  \ud835 \udcd3
 𝓸  \u{1d4f8}  \ud835 \udcf8
 𝓰  \u{1d4f0}  \ud835 \udcf0

 𝓑  \u{1d4d1}  \ud835 \udcd1
 𝓻  \u{1d4fb}  \ud835 \udcfb
 𝓸  \u{1d4f8}  \ud835 \udcf8
 𝔀  \u{1d500}  \ud835 \udd00
 𝓷  \u{1d4f7}  \ud835 \udcf7

 𝖰  \u{1d5b0}  \ud835 \uddb0
 𝖡  \u{1d5a1}  \ud835 \udda1
 𝖥  \u{1d5a5}  \ud835 \udda5

 (fox) 狸  \u{72f8}  \u72f8
 (dog) 狗  \u{72d7}  \u72d7  

 Valid ID_Continue Unicode characters (Can't be first identifier character.)

 𝟚  \u{1d7da}  \ud835 \udfda
*/

var string = "The quick brown fox jumped over the lazy dog's back";
var string2 = "It is a dog eat dog world.";

let match = null;

assert.sameValue(string.match(/(?<animal>fox|dog)/).groups.animal, "fox");

match = string.match(/(?<𝑓𝑜𝑥>fox).*(?<𝓓𝓸𝓰>dog)/);
assert.sameValue(match.groups.𝑓𝑜𝑥, "fox");
assert.sameValue(match.groups.𝓓𝓸𝓰, "dog");
assert.sameValue(match[1], "fox");
assert.sameValue(match[2], "dog");

match = string.match(/(?<狸>fox).*(?<狗>dog)/);
assert.sameValue(match.groups.狸, "fox");
assert.sameValue(match.groups.狗, "dog");
assert.sameValue(match[1], "fox");
assert.sameValue(match[2], "dog");

assert.sameValue(string.match(/(?<𝓑𝓻𝓸𝔀𝓷>brown)/).groups.𝓑𝓻𝓸𝔀𝓷, "brown");
assert.sameValue(string.match(/(?<𝓑𝓻𝓸𝔀𝓷>brown)/).groups.\u{1d4d1}\u{1d4fb}\u{1d4f8}\u{1d500}\u{1d4f7}, "brown");
assert.sameValue(string.match(/(?<\u{1d4d1}\u{1d4fb}\u{1d4f8}\u{1d500}\u{1d4f7}>brown)/).groups.𝓑𝓻𝓸𝔀𝓷, "brown");
assert.sameValue(string.match(/(?<\u{1d4d1}\u{1d4fb}\u{1d4f8}\u{1d500}\u{1d4f7}>brown)/).groups.\u{1d4d1}\u{1d4fb}\u{1d4f8}\u{1d500}\u{1d4f7}, "brown");
assert.sameValue(string.match(/(?<\ud835\udcd1\ud835\udcfb\ud835\udcf8\ud835\udd00\ud835\udcf7>brown)/).groups.𝓑𝓻𝓸𝔀𝓷, "brown");
assert.sameValue(string.match(/(?<\ud835\udcd1\ud835\udcfb\ud835\udcf8\ud835\udd00\ud835\udcf7>brown)/).groups.\u{1d4d1}\u{1d4fb}\u{1d4f8}\u{1d500}\u{1d4f7}, "brown");

assert.sameValue(string.match(/(?<𝖰𝖡𝖥>q\w*\W\w*\W\w*)/).groups.𝖰𝖡𝖥, "quick brown fox");
assert.sameValue(string.match(/(?<𝖰𝖡\u{1d5a5}>q\w*\W\w*\W\w*)/).groups.𝖰𝖡𝖥, "quick brown fox");
assert.sameValue(string.match(/(?<𝖰\u{1d5a1}𝖥>q\w*\W\w*\W\w*)/).groups.𝖰𝖡𝖥, "quick brown fox");
assert.sameValue(string.match(/(?<𝖰\u{1d5a1}\u{1d5a5}>q\w*\W\w*\W\w*)/).groups.𝖰𝖡𝖥, "quick brown fox");
assert.sameValue(string.match(/(?<\u{1d5b0}𝖡𝖥>q\w*\W\w*\W\w*)/).groups.𝖰𝖡𝖥, "quick brown fox");
assert.sameValue(string.match(/(?<\u{1d5b0}𝖡\u{1d5a5}>q\w*\W\w*\W\w*)/).groups.𝖰𝖡𝖥, "quick brown fox");
assert.sameValue(string.match(/(?<\u{1d5b0}\u{1d5a1}𝖥>q\w*\W\w*\W\w*)/).groups.𝖰𝖡𝖥, "quick brown fox");
assert.sameValue(string.match(/(?<\u{1d5b0}\u{1d5a1}\u{1d5a5}>q\w*\W\w*\W\w*)/).groups.𝖰𝖡𝖥, "quick brown fox");

assert.sameValue(string.match(/(?<the𝟚>the)/).groups.the𝟚, "the");
assert.sameValue(string.match(/(?<the\u{1d7da}>the)/).groups.the𝟚, "the");
assert.sameValue(string.match(/(?<the\ud835\udfda>the)/).groups.the𝟚, "the");

match = string2.match(/(?<dog>dog)(.*?)(\k<dog>)/);
assert.sameValue(match.groups.dog, "dog");
assert.sameValue(match[1], "dog");
assert.sameValue(match[2], " eat ");
assert.sameValue(match[3], "dog");

match = string2.match(/(?<𝓓𝓸𝓰>dog)(.*?)(\k<𝓓𝓸𝓰>)/);
assert.sameValue(match.groups.𝓓𝓸𝓰, "dog");
assert.sameValue(match[1], "dog");
assert.sameValue(match[2], " eat ");
assert.sameValue(match[3], "dog");

match = string2.match(/(?<狗>dog)(.*?)(\k<狗>)/);
assert.sameValue(match.groups.狗, "dog");
assert.sameValue(match[1], "dog");
assert.sameValue(match[2], " eat ");
assert.sameValue(match[3], "dog");
