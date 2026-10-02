// dns.lookup(name, callback): the pending lookup keeps the process alive until
// its callback has run, as a pending request does in Node.
//
// Before the fix the lookup ran on the I/O pool and nothing counted it as
// outstanding work, so a script whose only remaining work was the lookup ended
// before the callback ran -- silently, with status 0. This script schedules
// nothing else, so it is the lookup alone that must keep the process alive.
//
// 'localhost' resolves from the hosts file on every platform the CI runs (no
// network access is needed). The callback receives (null, address, family) on
// success. The script also runs unchanged under Node, which is how the
// expectations were checked. ctest requires the final line, so a callback that
// never runs cannot pass for success.
'use strict';
const dns = require('dns');

let checks = 0;
function check(cond, what) {
  checks++;
  if (!cond) throw new Error('test_dns_lookup_callback: FAILED: ' + what);
}

let returned = false;
dns.lookup('localhost', function (err, address, family) {
  check(returned, 'the callback runs after lookup() has returned');
  check(err === null, 'err is null on success (got ' + err + ')');
  check(typeof address === 'string' && address.length > 0,
        'address is a non-empty string (got ' + address + ')');
  check(family === 4 || family === 6, 'family is 4 or 6 (got ' + family + ')');
  if (family === 4) check(address.indexOf('127.') === 0, 'IPv4 localhost is 127.x (got ' + address + ')');
  if (family === 6) check(address === '::1', 'IPv6 localhost is ::1 (got ' + address + ')');
  console.log('test_dns_lookup_callback: all ' + checks + ' checks passed');
});
returned = true;
