// Fixture: an exported function that captures a module-local variable.
// Exercises closure-to-module resolution after the module body has returned.
var count = 0;

module.exports = {
    inc: function () { count = count + 1; return count; },
    filename: __filename,
    dirname: __dirname
};
