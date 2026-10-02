// Adapted from Node.js v22.20.0 test/parallel/test-path-dirname.js (MIT licence; see
// node_shim.js for the notice). Node's test harness is replaced by
// ./node_shim.js; every other change is marked 'protoJS:'.
'use strict';
const common = require('./node_shim').common;
const assert = require('./node_shim').assert;
const path = require('path');

// protoJS: this file lives in tests/integration/path, not test/parallel, and
// protoJS spells __filename with '/' on Windows too, so either separator is
// accepted there.
assert.strictEqual(path.dirname(__filename).slice(-16),
                   common.isWindows && path.dirname(__filename).slice(-17, -16) !== '/' &&
                   __filename.indexOf('\\') !== -1 ?
                     'integration\\path' : 'integration/path');

assert.strictEqual(path.posix.dirname('/a/b/'), '/a');
assert.strictEqual(path.posix.dirname('/a/b'), '/a');
assert.strictEqual(path.posix.dirname('/a'), '/');
assert.strictEqual(path.posix.dirname(''), '.');
assert.strictEqual(path.posix.dirname('/'), '/');
assert.strictEqual(path.posix.dirname('////'), '/');
assert.strictEqual(path.posix.dirname('//a'), '//');
assert.strictEqual(path.posix.dirname('foo'), '.');

assert.strictEqual(path.win32.dirname('c:\\'), 'c:\\');
assert.strictEqual(path.win32.dirname('c:\\foo'), 'c:\\');
assert.strictEqual(path.win32.dirname('c:\\foo\\'), 'c:\\');
assert.strictEqual(path.win32.dirname('c:\\foo\\bar'), 'c:\\foo');
assert.strictEqual(path.win32.dirname('c:\\foo\\bar\\'), 'c:\\foo');
assert.strictEqual(path.win32.dirname('c:\\foo\\bar\\baz'), 'c:\\foo\\bar');
assert.strictEqual(path.win32.dirname('c:\\foo bar\\baz'), 'c:\\foo bar');
assert.strictEqual(path.win32.dirname('\\'), '\\');
assert.strictEqual(path.win32.dirname('\\foo'), '\\');
assert.strictEqual(path.win32.dirname('\\foo\\'), '\\');
assert.strictEqual(path.win32.dirname('\\foo\\bar'), '\\foo');
assert.strictEqual(path.win32.dirname('\\foo\\bar\\'), '\\foo');
assert.strictEqual(path.win32.dirname('\\foo\\bar\\baz'), '\\foo\\bar');
assert.strictEqual(path.win32.dirname('\\foo bar\\baz'), '\\foo bar');
assert.strictEqual(path.win32.dirname('c:'), 'c:');
assert.strictEqual(path.win32.dirname('c:foo'), 'c:');
assert.strictEqual(path.win32.dirname('c:foo\\'), 'c:');
assert.strictEqual(path.win32.dirname('c:foo\\bar'), 'c:foo');
assert.strictEqual(path.win32.dirname('c:foo\\bar\\'), 'c:foo');
assert.strictEqual(path.win32.dirname('c:foo\\bar\\baz'), 'c:foo\\bar');
assert.strictEqual(path.win32.dirname('c:foo bar\\baz'), 'c:foo bar');
assert.strictEqual(path.win32.dirname('file:stream'), '.');
assert.strictEqual(path.win32.dirname('dir\\file:stream'), 'dir');
assert.strictEqual(path.win32.dirname('\\\\unc\\share'),
                   '\\\\unc\\share');
assert.strictEqual(path.win32.dirname('\\\\unc\\share\\foo'),
                   '\\\\unc\\share\\');
assert.strictEqual(path.win32.dirname('\\\\unc\\share\\foo\\'),
                   '\\\\unc\\share\\');
assert.strictEqual(path.win32.dirname('\\\\unc\\share\\foo\\bar'),
                   '\\\\unc\\share\\foo');
assert.strictEqual(path.win32.dirname('\\\\unc\\share\\foo\\bar\\'),
                   '\\\\unc\\share\\foo');
assert.strictEqual(path.win32.dirname('\\\\unc\\share\\foo\\bar\\baz'),
                   '\\\\unc\\share\\foo\\bar');
assert.strictEqual(path.win32.dirname('/a/b/'), '/a');
assert.strictEqual(path.win32.dirname('/a/b'), '/a');
assert.strictEqual(path.win32.dirname('/a'), '/');
assert.strictEqual(path.win32.dirname(''), '.');
assert.strictEqual(path.win32.dirname('/'), '/');
assert.strictEqual(path.win32.dirname('////'), '/');
assert.strictEqual(path.win32.dirname('foo'), '.');

require('./node_shim').done('path/test-path-dirname');
