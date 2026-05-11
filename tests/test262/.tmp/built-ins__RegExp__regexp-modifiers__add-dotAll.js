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


// Copyright 2023 Ron Buckton. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Ron Buckton
description: >
  dotAll (`s`) modifier can be added via `(?s:)` or `(?s-:)`.
info: |
  Runtime Semantics: CompileAtom
  The syntax-directed operation CompileAtom takes arguments direction (forward or backward) and modifiers (a Modifiers Record) and returns a Matcher.

  Atom :: `(` `?` RegularExpressionFlags `:` Disjunction `)`
    1. Let addModifiers be the source text matched by RegularExpressionFlags.
    2. Let removeModifiers be the empty String.
    3. Let newModifiers be UpdateModifiers(modifiers, CodePointsToString(addModifiers), removeModifiers).
    4. Return CompileSubpattern of Disjunction with arguments direction and newModifiers.

  UpdateModifiers ( modifiers, add, remove )
  The abstract operation UpdateModifiers takes arguments modifiers (a Modifiers Record), add (a String), and remove (a String) and returns a Modifiers. It performs the following steps when called:

  1. Let dotAll be modifiers.[[DotAll]].
  2. Let ignoreCase be modifiers.[[IgnoreCase]].
  3. Let multiline be modifiers.[[Multiline]].
  4. If add contains "s", set dotAll to true.
  5. If add contains "i", set ignoreCase to true.
  6. If add contains "m", set multiline to true.
  7. If remove contains "s", set dotAll to false.
  8. If remove contains "i", set ignoreCase to false.
  9. If remove contains "m", set multiline to false.
  10. Return the Modifiers Record { [[DotAll]]: dotAll, [[IgnoreCase]]: ignoreCase, [[Multiline]]: multiline }.

esid: sec-compileatom
features: [regexp-modifiers]
---*/

var re1 = /(?s:^.$)/;
assert(re1.test("a"), "Pattern character '.' should match non-line terminators in modified group");
assert(re1.test("3"), "Pattern character '.' should match non-line terminators in modified group");
assert(re1.test("π"), "Pattern character '.' should match non-line terminators in modified group");
assert(re1.test("\u2027"), "Pattern character '.' should match non-line terminators in modified group");
assert(re1.test("\u0085"), "Pattern character '.' should match non-line terminators in modified group");
assert(re1.test("\v"), "Pattern character '.' should match mon-line terminators in modified group");
assert(re1.test("\f"), "Pattern character '.' should match mon-line terminators in modified group");
assert(re1.test("\u180E"), "Pattern character '.' should match non-line terminators in modified group");
assert(!re1.test("\u{10300}"), "Supplementary plane not matched by a single .");
assert(re1.test("\n"), "Pattern character '.' should match line terminators in modified group");
assert(re1.test("\r"), "Pattern character '.' should match line terminators in modified group");
assert(re1.test("\u2028"), "Pattern character '.' should match line terminators in modified group");
assert(re1.test("\u2029"), "Pattern character '.' should match line terminators in modified group");
assert(re1.test("\uD800"), "Pattern character '.' should match non-line terminators in modified group");
assert(re1.test("\uDFFF"), "Pattern character '.' should match non-line terminators in modified group");

var re2 = new RegExp("(?s:^.$)");
assert(re2.test("a"), "Pattern character '.' should match non-line terminators in modified group");
assert(re2.test("3"), "Pattern character '.' should match non-line terminators in modified group");
assert(re2.test("π"), "Pattern character '.' should match non-line terminators in modified group");
assert(re2.test("\u2027"), "Pattern character '.' should match non-line terminators in modified group");
assert(re2.test("\u0085"), "Pattern character '.' should match non-line terminators in modified group");
assert(re2.test("\v"), "Pattern character '.' should match mon-line terminators in modified group");
assert(re2.test("\f"), "Pattern character '.' should match mon-line terminators in modified group");
assert(re2.test("\u180E"), "Pattern character '.' should match non-line terminators in modified group");
assert(!re2.test("\u{10300}"), "Supplementary plane not matched by a single .");
assert(re2.test("\n"), "Pattern character '.' should match line terminators in modified group");
assert(re2.test("\r"), "Pattern character '.' should match line terminators in modified group");
assert(re2.test("\u2028"), "Pattern character '.' should match line terminators in modified group");
assert(re2.test("\u2029"), "Pattern character '.' should match line terminators in modified group");
assert(re2.test("\uD800"), "Pattern character '.' should match non-line terminators in modified group");
assert(re2.test("\uDFFF"), "Pattern character '.' should match non-line terminators in modified group");

var re3 = /(?s-:^.$)/;
assert(re3.test("a"), "Pattern character '.' should match non-line terminators in modified group");
assert(re3.test("3"), "Pattern character '.' should match non-line terminators in modified group");
assert(re3.test("π"), "Pattern character '.' should match non-line terminators in modified group");
assert(re3.test("\u2027"), "Pattern character '.' should match non-line terminators in modified group");
assert(re3.test("\u0085"), "Pattern character '.' should match non-line terminators in modified group");
assert(re3.test("\v"), "Pattern character '.' should match mon-line terminators in modified group");
assert(re3.test("\f"), "Pattern character '.' should match mon-line terminators in modified group");
assert(re3.test("\u180E"), "Pattern character '.' should match non-line terminators in modified group");
assert(!re3.test("\u{10300}"), "Supplementary plane not matched by a single .");
assert(re3.test("\n"), "Pattern character '.' should match line terminators in modified group");
assert(re3.test("\r"), "Pattern character '.' should match line terminators in modified group");
assert(re3.test("\u2028"), "Pattern character '.' should match line terminators in modified group");
assert(re3.test("\u2029"), "Pattern character '.' should match line terminators in modified group");
assert(re3.test("\uD800"), "Pattern character '.' should match non-line terminators in modified group");
assert(re3.test("\uDFFF"), "Pattern character '.' should match non-line terminators in modified group");

var re4 = new RegExp("(?s-:^.$)");
assert(re4.test("a"), "Pattern character '.' should match non-line terminators in modified group");
assert(re4.test("3"), "Pattern character '.' should match non-line terminators in modified group");
assert(re4.test("π"), "Pattern character '.' should match non-line terminators in modified group");
assert(re4.test("\u2027"), "Pattern character '.' should match non-line terminators in modified group");
assert(re4.test("\u0085"), "Pattern character '.' should match non-line terminators in modified group");
assert(re4.test("\v"), "Pattern character '.' should match mon-line terminators in modified group");
assert(re4.test("\f"), "Pattern character '.' should match mon-line terminators in modified group");
assert(re4.test("\u180E"), "Pattern character '.' should match non-line terminators in modified group");
assert(!re4.test("\u{10300}"), "Supplementary plane not matched by a single .");
assert(re4.test("\n"), "Pattern character '.' should match line terminators in modified group");
assert(re4.test("\r"), "Pattern character '.' should match line terminators in modified group");
assert(re4.test("\u2028"), "Pattern character '.' should match line terminators in modified group");
assert(re4.test("\u2029"), "Pattern character '.' should match line terminators in modified group");
assert(re4.test("\uD800"), "Pattern character '.' should match non-line terminators in modified group");
assert(re4.test("\uDFFF"), "Pattern character '.' should match non-line terminators in modified group");

var re5 = /a.(?s:b.b).c/;
assert(re5.test("a,b,b,c"), "Pattern character '.' should match non-line terminators in modified group");
assert(re5.test("a,b\nb,c"), "Pattern character '.' should match line terminators in modified group");
assert(!re5.test("a\nb\nb,c"), "Pattern character '.' should not match line terminators outside modified group");
assert(!re5.test("a,b\nb\nc"), "Pattern character '.' should not match line terminators outside modified group");
assert(!re5.test("a\nb\nb\nc"), "Pattern character '.' should not match line terminators outside modified group");

var re6 = new RegExp("a.(?s:b.b).c");
assert(re6.test("a,b,b,c"), "Pattern character '.' should match non-line terminators in modified group");
assert(re6.test("a,b\nb,c"), "Pattern character '.' should match line terminators in modified group");
assert(!re6.test("a\nb\nb,c"), "Pattern character '.' should not match line terminators outside modified group");
assert(!re6.test("a,b\nb\nc"), "Pattern character '.' should not match line terminators outside modified group");
assert(!re6.test("a\nb\nb\nc"), "Pattern character '.' should not match line terminators outside modified group");
