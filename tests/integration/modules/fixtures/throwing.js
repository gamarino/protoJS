// Fixture: a module whose body throws. The error must reach the caller, and
// the module must not stay in the cache as a half-built entry.
throw new Error('module blew up');
