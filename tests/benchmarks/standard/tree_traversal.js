// Realistic benchmark: recursive tree traversal.
// Builds a binary tree and computes sum of all nodes.
// Exercises: recursion, object creation, property access, no JIT-friendly linear pattern.

const ITERATIONS = 5;
const DEPTH = 14; // 2^14-1 = 16383 nodes

function makeTree(depth) {
    if (depth === 0) return { val: 1, left: null, right: null };
    return {
        val: depth,
        left:  makeTree(depth - 1),
        right: makeTree(depth - 1)
    };
}

function sumTree(node) {
    if (!node) return 0;
    return node.val + sumTree(node.left) + sumTree(node.right);
}

var times = [];
for (var k = 0; k < ITERATIONS; k++) {
    var tree = makeTree(DEPTH);
    var start = Date.now();
    var sum = sumTree(tree);
    times.push(Date.now() - start);
}
times.sort(function(a, b) { return a - b; });
var median = times[Math.floor(ITERATIONS / 2)];
// The benchmark reports the sum it computed and checks it, so a crash or an
// early stop cannot pass for a fast run: S(0) = 1, S(d) = d + 2 * S(d - 1).
var expectedSum = 1;
for (var d = 1; d <= DEPTH; d++) expectedSum = d + 2 * expectedSum;
if (sum !== expectedSum) {
    throw new Error('tree_traversal: sum = ' + sum + ', expected ' + expectedSum);
}
var result = { name: 'tree_traversal', time_ms: median, iterations: ITERATIONS, depth: DEPTH, sum: sum };
console.log('__BENCH_RESULT__' + JSON.stringify(result));
