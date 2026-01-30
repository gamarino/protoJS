# protoCore Module Guide

## Introduction

The `protoCore` module exposes special collections and utilities from protoCore that have no direct equivalent in standard JavaScript.

## Available Collections

### ProtoSet

Similar to JavaScript's `Set`, but with special features (immutability, hash-based).

```javascript
const set = new protoCore.Set([1, 2, 3, 3, 4]);
console.log(set.size); // 4 (duplicates removed)

set.add(5);
console.log(set.size); // 5

console.log(set.has(3)); // true
set.remove(3);
console.log(set.has(3)); // false
```

### ProtoMultiset

Does not exist in standard JavaScript. Allows duplicate elements and counts occurrences.

```javascript
const multiset = new protoCore.Multiset([1, 1, 2, 2, 2, 3]);
console.log(multiset.size); // 6 (total including duplicates)
console.log(multiset.count(2)); // 3 (occurrences of 2)

multiset.add(2);
console.log(multiset.count(2)); // 4
```

### ProtoSparseList

Similar to `Array`, but optimized for sparse arrays (with gaps).

```javascript
const sparse = new protoCore.SparseList();
sparse.set(0, "first");
sparse.set(100, "hundredth");
sparse.set(1000, "thousandth");

console.log(sparse.size); // 3
console.log(sparse.get(0)); // "first"
console.log(sparse.has(50)); // false
```

### ProtoTuple

Immutable array, similar to tuples in other languages.

```javascript
const tuple = protoCore.Tuple([1, 2, 3]);
console.log(tuple.length); // 3
console.log(tuple[0]); // 1

// tuple.push(4); // Error: immutable
// tuple[0] = 10; // Error: immutable
```

## Mutability Control

### Creating Immutable Objects

```javascript
const immutable = protoCore.ImmutableObject({a: 1, b: 2});
// immutable.a = 3; // Error or creates new object
```

### Creating Mutable Objects

```javascript
const mutable = protoCore.MutableObject({a: 1, b: 2});
mutable.a = 3; // OK
console.log(mutable.a); // 3
```

### Checking Mutability

```javascript
const obj = {a: 1};
console.log(protoCore.isImmutable(obj)); // false (JS objects are mutable by default)

const immutable = protoCore.ImmutableObject({a: 1});
console.log(protoCore.isImmutable(immutable)); // true
```

### Converting Mutability

```javascript
const obj = {a: 1};

// Convert to immutable
const immutable = protoCore.makeImmutable(obj);

// Convert to mutable
const mutable = protoCore.makeMutable(immutable);
```

## Advantages

- **Immutability**: Eliminates shared state bugs
- **Efficiency**: Structural sharing reduces memory usage
- **Concurrency**: Immutable objects are shared between threads without copying
- **Advanced collections**: Multiset and others not available in standard JS

## Module Discovery (protoCore)

protoCore provides a **Unified Module Discovery and Provider System**: configurable resolution chain per `ProtoSpace`, `ProviderRegistry` of `ModuleProvider`s, and `getImportModule` with a shared, thread-safe module cache. protoJS uses a `ProtoSpace` per context; that space carries the resolution chain and module roots.

For how protoJS integrates with this system and when to use `getImportModule` from host code, see **[MODULE_DISCOVERY_PROTOCORE.md](MODULE_DISCOVERY_PROTOCORE.md)**. For the full specification, see protoCore’s `docs/MODULE_DISCOVERY.md`.
