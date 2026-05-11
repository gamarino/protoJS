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


// Copyright (c) 2012 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
es5id: 7.6.1-4-2
description: >
    Allow reserved words as property names by accessor functions within an object.
---*/

var test;

var tokenCodes = {
    set await(value) { test = "await"; },
    get await() { return "await"; },
    set break(value) { test = "break"; },
    get break() { return "break"; },
    set case(value) { test = "case"; },
    get case() { return "case"; },
    set catch(value) { test = "catch"; },
    get catch() { return "catch"; },
    set class(value) { test = "class"; },
    get class() { return "class"; },
    set const(value) { test = "const"; },
    get const() { return "const"; },
    set continue(value) { test = "continue"; },
    get continue() { return "continue"; },
    set debugger(value) { test = "debugger"; },
    get debugger() { return "debugger"; },
    set default(value) { test = "default"; },
    get default() { return "default"; },
    set delete(value) { test = "delete"; },
    get delete() { return "delete"; },
    set do(value) { test = "do"; },
    get do() { return "do"; },
    set else(value) { test = "else"; },
    get else() { return "else"; },
    set export(value) { test = "export"; },
    get export() { return "export"; },
    set extends(value) { test = "extends"; },
    get extends() { return "extends"; },
    set finally(value) { test = "finally"; },
    get finally() { return "finally"; },
    set for(value) { test = "for"; },
    get for() { return "for"; },
    set function(value) { test = "function"; },
    get function() { return "function"; },
    set if(value) { test = "if"; },
    get if() { return "if"; },
    set import(value) { test = "import"; },
    get import() { return "import"; },
    set in(value) { test = "in"; },
    get in() { return "in"; },
    set instanceof(value) { test = "instanceof"; },
    get instanceof() { return "instanceof"; },
    set new(value) { test = "new"; },
    get new() { return "new"; },
    set return(value) { test = "return"; },
    get return() { return "return"; },
    set super(value) { test = "super"; },
    get super() { return "super"; },
    set switch(value) { test = "switch"; },
    get switch() { return "switch"; },
    set this(value) { test = "this"; },
    get this() { return "this"; },
    set throw(value) { test = "throw"; },
    get throw() { return "throw"; },
    set try(value) { test = "try"; },
    get try() { return "try"; },
    set typeof(value) { test = "typeof"; },
    get typeof() { return "typeof"; },
    set var(value) { test = "var"; },
    get var() { return "var"; },
    set void(value) { test = "void"; },
    get void() { return "void"; },
    set while(value) { test = "while"; },
    get while() { return "while"; },
    set with(value) { test = "with"; },
    get with() { return "with"; },
    set yield(value) { test = "yield"; },
    get yield() { return "yield"; },

    set enum(value) { test = "enum"; },
    get enum() { return "enum"; },

    set implements(value) { test = "implements"; },
    get implements() { return "implements"; },
    set interface(value) { test = "interface"; },
    get interface() { return "interface"; },
    set package(value) { test = "package"; },
    get package() { return "package"; },
    set private(value) { test = "private"; },
    get private() { return "private"; },
    set protected(value) { test = "protected"; },
    get protected() { return "protected"; },
    set public(value) { test = "public"; },
    get public() { return "public"; },

    set let(value) { test = "let"; },
    get let() { return "let"; },
    set static(value) { test = "static"; },
    get static() { return "static"; },
};

var arr = [
    'await',
    'break',
    'case',
    'catch',
    'class',
    'const',
    'continue',
    'debugger',
    'default',
    'delete',
    'do',
    'else',
    'export',
    'extends',
    'finally',
    'for',
    'function',
    'if',
    'import',
    'in',
    'instanceof',
    'new',
    'return',
    'super',
    'switch',
    'this',
    'throw',
    'try',
    'typeof',
    'var',
    'void',
    'while',
    'with',
    'yield',

    'enum',

    'implements',
    'interface',
    'package',
    'protected',
    'private',
    'public',

    'let',
    'static',
];

for (var i = 0; i < arr.length; ++i) {
    var propertyName = arr[i];

    assert(tokenCodes.hasOwnProperty(propertyName),
           'Property "' + propertyName + '" found');

    assert.sameValue(tokenCodes[propertyName], propertyName,
                     'Property "' + propertyName + '" has correct value');

    tokenCodes[propertyName] = 0;
    assert.sameValue(test, propertyName,
                     'Property "' + propertyName + '" sets correct value');
}
