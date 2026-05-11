const p = new Proxy([], {});
console.log('JS says Array.isArray(proxy):', Array.isArray(p));
