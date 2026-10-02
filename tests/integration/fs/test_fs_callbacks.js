// fs callback API: readFile, writeFile, appendFile, stat, readdir, unlink,
// rmdir and mkdir with a trailing callback, as in Node.
//
// Each callback is invoked asynchronously on the event loop -- after the call
// has returned -- with (err) on failure and (null, result) on success; err is
// an Error with Node's code, errno, syscall, path and message. The operations
// run one after another in a temporary directory, which is removed at the end.
//
// The first failed check throws (exit status 1). The script also runs
// unchanged under Node, which is how the expectations were checked. ctest
// requires the final "all N checks passed" line, so a callback that never runs
// cannot pass for success.
'use strict';
const fs = require('fs');
const path = require('path');

const platform = typeof process.platform === 'function' ?
  process.platform() : process.platform;
const env = process.env || {};
const tmp = env.TMPDIR || env.TEMP || env.TMP || (platform === 'win32' ? '.' : '/tmp');
const dir = path.join(tmp, 'protojs-fs-cb-' + Date.now() + '-' +
                      Math.floor(Math.random() * 1e9));
const file = path.join(dir, 'f.txt');
const missing = path.join(dir, 'missing.txt');

let checks = 0;
function check(cond, what) {
  checks++;
  if (!cond) throw new Error('test_fs_callbacks: FAILED: ' + what);
}

// Node's error shape: an Error with code, a negative errno, syscall and path,
// and the message "<CODE>: <description>, <syscall> '<path>'" (without the
// path when the error has none, as for EISDIR from read).
function checkError(err, code, syscall, p, description) {
  check(err instanceof Error, syscall + ': err is an Error');
  check(err.code === code, syscall + ': err.code ' + err.code + ' === ' + code);
  check(err.syscall === syscall, syscall + ': err.syscall ' + err.syscall);
  check(err.path === p, syscall + ': err.path ' + err.path);
  check(typeof err.errno === 'number' && err.errno < 0,
        syscall + ': err.errno is negative: ' + err.errno);
  if (code === 'ENOENT' && (platform === 'linux' || platform === 'darwin'))
    check(err.errno === -2, syscall + ': ENOENT errno is -2: ' + err.errno);
  const where = p === undefined ? '' : " '" + p + "'";
  check(err.message === code + ': ' + description + ', ' + syscall + where,
        syscall + ': err.message ' + JSON.stringify(err.message));
}

// Run fn(callback) and check that the callback does not run synchronously.
function async(fn, next) {
  let returned = false;
  fn(function () {
    check(returned, 'callback invoked asynchronously');
    next.apply(null, arguments);
  });
  returned = true;
}

const steps = [
  (next) => async((cb) => fs.mkdir(dir, cb), (err) => {
    check(err === null, 'mkdir: err is null: ' + err);
    next();
  }),
  (next) => async((cb) => fs.mkdir(dir, cb), (err) => {
    checkError(err, 'EEXIST', 'mkdir', dir, 'file already exists');
    next();
  }),
  (next) => async((cb) => fs.writeFile(file, 'hello', cb), (err) => {
    check(err === null, 'writeFile: err is null: ' + err);
    next();
  }),
  (next) => async((cb) => fs.appendFile(file, ' world', cb), (err) => {
    check(err === null, 'appendFile: err is null: ' + err);
    next();
  }),
  (next) => async((cb) => fs.readFile(file, 'utf8', cb), (err, data) => {
    check(err === null, 'readFile: err is null: ' + err);
    check(data === 'hello world', 'readFile: data ' + JSON.stringify(data));
    next();
  }),
  (next) => async((cb) => fs.writeFile(file, '!', { flag: 'a' }, cb), (err) => {
    check(err === null, "writeFile flag 'a': err is null: " + err);
    next();
  }),
  (next) => async((cb) => fs.readFile(file, { encoding: 'utf8' }, cb), (err, data) => {
    check(err === null, 'readFile options: err is null: ' + err);
    check(data === 'hello world!', 'readFile options: data ' + JSON.stringify(data));
    next();
  }),
  (next) => async((cb) => fs.readFile(missing, 'utf8', cb), (err, data) => {
    checkError(err, 'ENOENT', 'open', missing, 'no such file or directory');
    check(data === undefined, 'readFile error: no data');
    next();
  }),
  (next) => async((cb) => fs.readFile(dir, 'utf8', cb), (err) => {
    checkError(err, 'EISDIR', 'read', undefined, 'illegal operation on a directory');
    next();
  }),
  (next) => async((cb) => fs.stat(file, cb), (err, st) => {
    check(err === null, 'stat: err is null: ' + err);
    check(st.size === 12, 'stat: size ' + st.size);
    // protoJS's Stats carries isFile / isDirectory as booleans (as statSync
    // does); Node's as methods.
    const isFile = typeof st.isFile === 'function' ? st.isFile() : st.isFile;
    const isDir = typeof st.isDirectory === 'function' ? st.isDirectory() : st.isDirectory;
    check(isFile === true && isDir === false, 'stat: a file');
    next();
  }),
  (next) => async((cb) => fs.stat(missing, cb), (err) => {
    checkError(err, 'ENOENT', 'stat', missing, 'no such file or directory');
    next();
  }),
  (next) => async((cb) => fs.readdir(dir, cb), (err, names) => {
    check(err === null, 'readdir: err is null: ' + err);
    check(Array.isArray(names) && names.length === 1 && names[0] === 'f.txt',
          'readdir: names ' + JSON.stringify(names));
    next();
  }),
  (next) => async((cb) => fs.readdir(missing, cb), (err) => {
    checkError(err, 'ENOENT', 'scandir', missing, 'no such file or directory');
    next();
  }),
  (next) => async((cb) => fs.mkdir(path.join(dir, 'a', 'b'), { recursive: true }, cb),
                  (err, first) => {
    check(err === null, 'mkdir recursive: err is null: ' + err);
    check(first === path.join(dir, 'a'), 'mkdir recursive: first ' + first);
    next();
  }),
  (next) => async((cb) => fs.mkdir(path.join(dir, 'a', 'b'), { recursive: true }, cb),
                  (err, first) => {
    check(err === null, 'mkdir recursive, existing: err is null: ' + err);
    check(first === undefined, 'mkdir recursive, existing: first ' + first);
    next();
  }),
  (next) => async((cb) => fs.unlink(file, cb), (err) => {
    check(err === null, 'unlink: err is null: ' + err);
    next();
  }),
  (next) => async((cb) => fs.unlink(file, cb), (err) => {
    checkError(err, 'ENOENT', 'unlink', file, 'no such file or directory');
    next();
  }),
  (next) => async((cb) => fs.rmdir(dir, cb), (err) => {
    checkError(err, 'ENOTEMPTY', 'rmdir', dir, 'directory not empty');
    next();
  }),
  (next) => async((cb) => fs.rmdir(path.join(dir, 'a', 'b'), cb), (err) => {
    check(err === null, 'rmdir: err is null: ' + err);
    fs.rmdir(path.join(dir, 'a'), (err2) => {
      check(err2 === null, 'rmdir a: err is null: ' + err2);
      fs.rmdir(dir, (err3) => {
        check(err3 === null, 'rmdir dir: err is null: ' + err3);
        next();
      });
    });
  }),
];

// Invalid arguments throw synchronously, as in Node.
function checkThrows(fn, what) {
  let error = null;
  try {
    fn();
  } catch (e) {
    error = e;
  }
  check(error instanceof TypeError, what + ': throws a TypeError');
  check(error && error.code === 'ERR_INVALID_ARG_TYPE', what + ': code ' +
        (error && error.code));
}
checkThrows(() => fs.readFile(file), 'readFile without a callback');
checkThrows(() => fs.readFile(file, 'utf8'), 'readFile with options and no callback');
checkThrows(() => fs.readFile(true, () => {}), 'readFile with a boolean path');
checkThrows(() => fs.stat(undefined, () => {}), 'stat with an undefined path');

let step = 0;
function next() {
  if (step === steps.length) {
    console.log('test_fs_callbacks: all ' + checks + ' checks passed');
    return;
  }
  steps[step++](next);
}
next();
