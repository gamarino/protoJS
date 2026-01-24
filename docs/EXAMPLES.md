# Advanced Examples

Collection of advanced examples for protoJS.

## Immutable Arrays

In protoJS, arrays are immutable by default when converted to protoCore.

```javascript
const original = [1, 2, 3];
const withFour = original.concat([4]);
console.log("Original:", original); // [1, 2, 3] - unchanged
console.log("New:", withFour);      // [1, 2, 3, 4]
```

## Concurrent Deferred

Deferred automatically executes in worker threads.

```javascript
const deferreds = [];
for (let i = 0; i < 10; i++) {
    const d = new Deferred((resolve) => {
        let sum = 0;
        for (let j = 0; j < 1000000; j++) {
            sum += j;
        }
        resolve({index: i, sum: sum});
    });
    deferreds.push(d);
}
```

## protoCore Collections

### ProtoSet

```javascript
const set = new protoCore.Set([1, 2, 3, 3, 4]);
console.log(set.size); // 4
set.add(5);
console.log(set.has(3)); // true
```

### ProtoMultiset

```javascript
const multiset = new protoCore.Multiset([1, 1, 2, 2, 2]);
console.log(multiset.count(2)); // 3
console.log(multiset.size); // 5
```

### ProtoSparseList

```javascript
const sparse = new protoCore.SparseList();
sparse.set(0, "first");
sparse.set(100, "hundredth");
console.log(sparse.get(0)); // "first"
console.log(sparse.has(50)); // false
```

### ProtoTuple

```javascript
const tuple = protoCore.Tuple([1, 2, 3]);
console.log(tuple.length); // 3
// tuple.push(4); // Error: immutable
```

## Mutability Control

```javascript
const immutable = protoCore.ImmutableObject({a: 1, b: 2});
const mutable = protoCore.MutableObject({a: 1, b: 2});
mutable.a = 3; // OK
console.log(protoCore.isImmutable(immutable)); // true
```

## I/O Operations

```javascript
const content = io.readFile("data.txt");
console.log(content);
io.writeFile("output.txt", "Hello, world!");
```

## Process Information

```javascript
console.log("Args:", process.argv);
console.log("Platform:", process.platform());
console.log("Arch:", process.arch());
console.log("CWD:", process.cwd());
```

## References

- [API Reference](API_REFERENCE.md)
- [Deferred Guide](DEFERRED_USAGE.md)
- [protoCore Module](PROTOCORE_MODULE.md)
