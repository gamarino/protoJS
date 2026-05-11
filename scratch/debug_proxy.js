const p = new Proxy([], {});
console.log('Array.isArray(new Proxy([], {})):', Array.isArray(p));
if (Array.isArray(p)) {
    console.log('PASS: Proxy to array is identified as array');
} else {
    console.log('FAIL: Proxy to array should be an array');
}

const { proxy: p2, revoke } = Proxy.revocable([], {});
revoke();
try {
    Array.isArray(p2);
    console.log('FAIL: Array.isArray(revokedProxy) should throw TypeError');
} catch (e) {
    console.log('PASS: Array.isArray(revokedProxy) threw:', e.name);
}
