// Minimal sta.js stub for local Test262-style runs.

function $DONOTEVALUATE() {
  throw new Error("$DONOTEVALUATE called");
}

function Test262Error(message) {
  this.name = "Test262Error";
  this.message = String(message || "");
}
Test262Error.prototype = Object.create(Error.prototype);
Test262Error.prototype.constructor = Test262Error;

