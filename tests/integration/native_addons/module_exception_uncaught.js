// An error of a module written in C++ that no handler catches ends the
// program as an uncaught JavaScript error does: reported as
// "<name>: <message>" from the error's `name` and `message` attributes, with
// exit status 1 (ctest matches the report; see tests/CMakeLists.txt).
// argv[2] is the geometry addon (test_module_exceptions.js explains it).
require(process.argv[2]);
var geometry = require('example/geometry');
console.log("before");
geometry.circleArea(-1);
console.log("not reached");
