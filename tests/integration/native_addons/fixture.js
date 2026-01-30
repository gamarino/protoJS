// Used by test_resolution.js: when both fixture.js and fixture.so exist,
// require.resolve('./fixture') should resolve to the native addon (fixture.so).
module.exports = { type: 'js' };
