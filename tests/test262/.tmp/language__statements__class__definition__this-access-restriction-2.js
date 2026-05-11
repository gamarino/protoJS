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


// Copyright (C) 2014 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
es6id: 14.5
description: >
    class this access restriction 2
---*/
class Base {
  constructor(a, b) {
    var o = new Object();
    o.prp = a + b;
    return o;
  }
}

class Subclass extends Base {
  constructor(a, b) {
    var exn;
    try {
      this.prp1 = 3;
    } catch (e) {
      exn = e;
    }
    assert.sameValue(
      exn instanceof ReferenceError,
      true,
      "The result of `exn instanceof ReferenceError` is `true`"
    );
    super(a, b);
    assert.sameValue(this.prp, a + b, "The value of `this.prp` is `a + b`");
    assert.sameValue(this.prp1, undefined, "The value of `this.prp1` is `undefined`");
    assert.sameValue(
      this.hasOwnProperty("prp1"),
      false,
      "`this.hasOwnProperty(\"prp1\")` returns `false`"
    );
    return this;
  }
}

var b = new Base(1, 2);
assert.sameValue(b.prp, 3, "The value of `b.prp` is `3`");


var s = new Subclass(2, -1);
assert.sameValue(s.prp, 1, "The value of `s.prp` is `1`");
assert.sameValue(s.prp1, undefined, "The value of `s.prp1` is `undefined`");
assert.sameValue(
  s.hasOwnProperty("prp1"),
  false,
  "`s.hasOwnProperty(\"prp1\")` returns `false`"
);

class Subclass2 extends Base {
  constructor(x) {
    super(1,2);

    if (x < 0) return;

    var called = false;
    function tmp() { called = true; return 3; }
    var exn = null;
    try {
      super(tmp(),4);
    } catch (e) { exn = e; }
    assert.sameValue(
      exn instanceof ReferenceError,
      true,
      "The result of `exn instanceof ReferenceError` is `true`"
    );
    assert.sameValue(called, true, "The value of `called` is `true`");
  }
}

var s2 = new Subclass2(1);
assert.sameValue(s2.prp, 3, "The value of `s2.prp` is `3`");

var s3 = new Subclass2(-1);
assert.sameValue(s3.prp, 3, "The value of `s3.prp` is `3`");

assert.throws(TypeError, function() { Subclass.call(new Object(), 1, 2); });
assert.throws(TypeError, function() { Base.call(new Object(), 1, 2); });

class BadSubclass extends Base {
  constructor() {}
}

assert.throws(ReferenceError, function() { new BadSubclass(); });
