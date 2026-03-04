// Minimal Test262-style assert harness for local runs.

var assert = {
  sameValue: function (actual, expected, message) {
    if (actual !== expected || (actual !== actual && expected === expected)) {
      throw new Error(
        (message || "Expected same value") +
          " (expected: " +
          String(expected) +
          ", actual: " +
          String(actual) +
          ")"
      );
    }
  },

  throws: function (ErrorConstructor, func, message) {
    var threw = false;
    try {
      func();
    } catch (e) {
      threw = true;
      if (ErrorConstructor && !(e instanceof ErrorConstructor)) {
        throw new Error(
          (message || "Expected error type") +
            " " +
            (ErrorConstructor.name || "") +
            ", got " +
            (e && e.constructor && e.constructor.name)
        );
      }
    }
    if (!threw) {
      throw new Error(message || "Expected function to throw");
    }
  }
};

