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


// Copyright (C) 2017 Josh Wolfe. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: exponentiation operator ToNumeric with BigInt operands
esid: sec-exp-operator-runtime-semantics-evaluation
features: [BigInt, Symbol.toPrimitive, computed-property-names, exponentiation]
---*/
function err() {
  throw new Test262Error();
}

function MyError() {}

assert.sameValue({
  [Symbol.toPrimitive]: function() {
    return 2n;
  },

  valueOf: err,
  toString: err
} ** 1n, 2n, 'The result of (({[Symbol.toPrimitive]: function() {return 2n;}, valueOf: err, toString: err}) ** 1n) is 2n');

assert.sameValue(1n ** {
  [Symbol.toPrimitive]: function() {
    return 2n;
  },

  valueOf: err,
  toString: err
}, 1n, 'The result of (1n ** {[Symbol.toPrimitive]: function() {return 2n;}, valueOf: err, toString: err}) is 1n');

assert.sameValue({
  valueOf: function() {
    return 2n;
  },

  toString: err
} ** 1n, 2n, 'The result of (({valueOf: function() {return 2n;}, toString: err}) ** 1n) is 2n');

assert.sameValue(1n ** {
  valueOf: function() {
    return 2n;
  },

  toString: err
}, 1n, 'The result of (1n ** {valueOf: function() {return 2n;}, toString: err}) is 1n');

assert.sameValue({
  toString: function() {
    return 2n;
  }
} ** 1n, 2n, 'The result of (({toString: function() {return 2n;}}) ** 1n) is 2n');

assert.sameValue(1n ** {
  toString: function() {
    return 2n;
  }
}, 1n, 'The result of (1n ** {toString: function() {return 2n;}}) is 1n');

assert.sameValue({
  [Symbol.toPrimitive]: undefined,

  valueOf: function() {
    return 2n;
  }
} ** 1n, 2n, 'The result of (({[Symbol.toPrimitive]: undefined, valueOf: function() {return 2n;}}) ** 1n) is 2n');

assert.sameValue(1n ** {
  [Symbol.toPrimitive]: undefined,

  valueOf: function() {
    return 2n;
  }
}, 1n, 'The result of (1n ** {[Symbol.toPrimitive]: undefined, valueOf: function() {return 2n;}}) is 1n');

assert.sameValue({
  [Symbol.toPrimitive]: null,

  valueOf: function() {
    return 2n;
  }
} ** 1n, 2n, 'The result of (({[Symbol.toPrimitive]: null, valueOf: function() {return 2n;}}) ** 1n) is 2n');

assert.sameValue(1n ** {
  [Symbol.toPrimitive]: null,

  valueOf: function() {
    return 2n;
  }
}, 1n, 'The result of (1n ** {[Symbol.toPrimitive]: null, valueOf: function() {return 2n;}}) is 1n');

assert.sameValue({
  valueOf: null,

  toString: function() {
    return 2n;
  }
} ** 1n, 2n, 'The result of (({valueOf: null, toString: function() {return 2n;}}) ** 1n) is 2n');

assert.sameValue(1n ** {
  valueOf: null,

  toString: function() {
    return 2n;
  }
}, 1n, 'The result of (1n ** {valueOf: null, toString: function() {return 2n;}}) is 1n');

assert.sameValue({
  valueOf: 1,

  toString: function() {
    return 2n;
  }
} ** 1n, 2n, 'The result of (({valueOf: 1, toString: function() {return 2n;}}) ** 1n) is 2n');

assert.sameValue(1n ** {
  valueOf: 1,

  toString: function() {
    return 2n;
  }
}, 1n, 'The result of (1n ** {valueOf: 1, toString: function() {return 2n;}}) is 1n');

assert.sameValue({
  valueOf: {},

  toString: function() {
    return 2n;
  }
} ** 1n, 2n, 'The result of (({valueOf: {}, toString: function() {return 2n;}}) ** 1n) is 2n');

assert.sameValue(1n ** {
  valueOf: {},

  toString: function() {
    return 2n;
  }
}, 1n, 'The result of (1n ** {valueOf: {}, toString: function() {return 2n;}}) is 1n');

assert.sameValue({
  valueOf: function() {
    return {};
  },

  toString: function() {
    return 2n;
  }
} ** 1n, 2n, 'The result of (({valueOf: function() {return {};}, toString: function() {return 2n;}}) ** 1n) is 2n');

assert.sameValue(1n ** {
  valueOf: function() {
    return {};
  },

  toString: function() {
    return 2n;
  }
}, 1n, 'The result of (1n ** {valueOf: function() {return {};}, toString: function() {return 2n;}}) is 1n');

assert.sameValue({
  valueOf: function() {
    return Object(12345);
  },

  toString: function() {
    return 2n;
  }
} ** 1n, 2n, 'The result of (({valueOf: function() {return Object(12345);}, toString: function() {return 2n;}}) ** 1n) is 2n');

assert.sameValue(1n ** {
  valueOf: function() {
    return Object(12345);
  },

  toString: function() {
    return 2n;
  }
}, 1n, 'The result of (1n ** {valueOf: function() {return Object(12345);}, toString: function() {return 2n;}}) is 1n');

assert.throws(TypeError, function() {
  ({
    [Symbol.toPrimitive]: 1
  }) ** 0n;
}, '({[Symbol.toPrimitive]: 1}) ** 0n throws TypeError');

assert.throws(TypeError, function() {
  0n ** {
    [Symbol.toPrimitive]: 1
  };
}, '0n ** {[Symbol.toPrimitive]: 1} throws TypeError');

assert.throws(TypeError, function() {
  ({
    [Symbol.toPrimitive]: {}
  }) ** 0n;
}, '({[Symbol.toPrimitive]: {}}) ** 0n throws TypeError');

assert.throws(TypeError, function() {
  0n ** {
    [Symbol.toPrimitive]: {}
  };
}, '0n ** {[Symbol.toPrimitive]: {}} throws TypeError');

assert.throws(TypeError, function() {
  ({
    [Symbol.toPrimitive]: function() {
      return Object(1);
    }
  }) ** 0n;
}, '({[Symbol.toPrimitive]: function() {return Object(1);}}) ** 0n throws TypeError');

assert.throws(TypeError, function() {
  0n ** {
    [Symbol.toPrimitive]: function() {
      return Object(1);
    }
  };
}, '0n ** {[Symbol.toPrimitive]: function() {return Object(1);}} throws TypeError');

assert.throws(TypeError, function() {
  ({
    [Symbol.toPrimitive]: function() {
      return {};
    }
  }) ** 0n;
}, '({[Symbol.toPrimitive]: function() {return {};}}) ** 0n throws TypeError');

assert.throws(TypeError, function() {
  0n ** {
    [Symbol.toPrimitive]: function() {
      return {};
    }
  };
}, '0n ** {[Symbol.toPrimitive]: function() {return {};}} throws TypeError');

assert.throws(MyError, function() {
  ({
    [Symbol.toPrimitive]: function() {
      throw new MyError();
    }
  }) ** 0n;
}, '({[Symbol.toPrimitive]: function() {throw new MyError();}}) ** 0n throws MyError');

assert.throws(MyError, function() {
  0n ** {
    [Symbol.toPrimitive]: function() {
      throw new MyError();
    }
  };
}, '0n ** {[Symbol.toPrimitive]: function() {throw new MyError();}} throws MyError');

assert.throws(MyError, function() {
  ({
    valueOf: function() {
      throw new MyError();
    }
  }) ** 0n;
}, '({valueOf: function() {throw new MyError();}}) ** 0n throws MyError');

assert.throws(MyError, function() {
  0n ** {
    valueOf: function() {
      throw new MyError();
    }
  };
}, '0n ** {valueOf: function() {throw new MyError();}} throws MyError');

assert.throws(MyError, function() {
  ({
    toString: function() {
      throw new MyError();
    }
  }) ** 0n;
}, '({toString: function() {throw new MyError();}}) ** 0n throws MyError');

assert.throws(MyError, function() {
  0n ** {
    toString: function() {
      throw new MyError();
    }
  };
}, '0n ** {toString: function() {throw new MyError();}} throws MyError');

assert.throws(TypeError, function() {
  ({
    valueOf: null,
    toString: null
  }) ** 0n;
}, '({valueOf: null, toString: null}) ** 0n throws TypeError');

assert.throws(TypeError, function() {
  0n ** {
    valueOf: null,
    toString: null
  };
}, '0n ** {valueOf: null, toString: null} throws TypeError');

assert.throws(TypeError, function() {
  ({
    valueOf: 1,
    toString: 1
  }) ** 0n;
}, '({valueOf: 1, toString: 1}) ** 0n throws TypeError');

assert.throws(TypeError, function() {
  0n ** {
    valueOf: 1,
    toString: 1
  };
}, '0n ** {valueOf: 1, toString: 1} throws TypeError');

assert.throws(TypeError, function() {
  ({
    valueOf: {},
    toString: {}
  }) ** 0n;
}, '({valueOf: {}, toString: {}}) ** 0n throws TypeError');

assert.throws(TypeError, function() {
  0n ** {
    valueOf: {},
    toString: {}
  };
}, '0n ** {valueOf: {}, toString: {}} throws TypeError');

assert.throws(TypeError, function() {
  ({
    valueOf: function() {
      return Object(1);
    },

    toString: function() {
      return Object(1);
    }
  }) ** 0n;
}, '({valueOf: function() {return Object(1);}, toString: function() {return Object(1);}}) ** 0n throws TypeError');

assert.throws(TypeError, function() {
  0n ** {
    valueOf: function() {
      return Object(1);
    },

    toString: function() {
      return Object(1);
    }
  };
}, '0n ** {valueOf: function() {return Object(1);}, toString: function() {return Object(1);}} throws TypeError');

assert.throws(TypeError, function() {
  ({
    valueOf: function() {
      return {};
    },

    toString: function() {
      return {};
    }
  }) ** 0n;
}, '({valueOf: function() {return {};}, toString: function() {return {};}}) ** 0n throws TypeError');

assert.throws(TypeError, function() {
  0n ** {
    valueOf: function() {
      return {};
    },

    toString: function() {
      return {};
    }
  };
}, '0n ** {valueOf: function() {return {};}, toString: function() {return {};}} throws TypeError');
