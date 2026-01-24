// Benchmark Runner Framework
// Provides reusable benchmarking infrastructure with statistical analysis

class BenchmarkRunner {
    constructor(name, iterations = 100, warmup = 10) {
        this.name = name;
        this.iterations = iterations;
        this.warmup = warmup;
        this.times = [];
        this.memoryBefore = null;
        this.memoryAfter = null;
    }

    // Get high-resolution timer
    static getTimer() {
        if (typeof performance !== 'undefined' && typeof performance.now === 'function') {
            return () => performance.now();
        } else if (typeof process !== 'undefined' && typeof process.hrtime === 'function') {
            // Node.js hrtime
            return () => {
                const [sec, nanosec] = process.hrtime();
                return sec * 1000 + nanosec / 1000000;
            };
        } else {
            // Fallback to Date.now() (millisecond precision)
            return () => Date.now();
        }
    }

    // Get memory usage if available
    static getMemoryUsage() {
        if (typeof process !== 'undefined' && typeof process.memoryUsage === 'function') {
            const usage = process.memoryUsage();
            return {
                heapUsed: usage.heapUsed,
                heapTotal: usage.heapTotal,
                external: usage.external || 0,
                rss: usage.rss || 0
            };
        }
        return null;
    }

    // Run benchmark with warmup and multiple iterations
    run(fn) {
        this.times = [];
        const timer = BenchmarkRunner.getTimer();
        
        // Warmup phase
        for (let i = 0; i < this.warmup; i++) {
            fn();
        }

        // Memory measurement before
        this.memoryBefore = BenchmarkRunner.getMemoryUsage();

        // Actual measurements
        for (let i = 0; i < this.iterations; i++) {
            const start = timer();
            fn();
            const end = timer();
            this.times.push(end - start);
        }

        // Memory measurement after
        this.memoryAfter = BenchmarkRunner.getMemoryUsage();

        return this.getStats();
    }

    // Calculate statistical measures
    getStats() {
        if (this.times.length === 0) {
            return {
                name: this.name,
                iterations: 0,
                min: 0,
                max: 0,
                mean: 0,
                median: 0,
                stddev: 0,
                total: 0,
                memory: null
            };
        }

        const sorted = [...this.times].sort((a, b) => a - b);
        const sum = this.times.reduce((a, b) => a + b, 0);
        const mean = sum / this.times.length;
        
        // Calculate standard deviation
        const variance = this.times.reduce((acc, time) => acc + Math.pow(time - mean, 2), 0) / this.times.length;
        const stddev = Math.sqrt(variance);
        
        // Calculate median
        const mid = Math.floor(sorted.length / 2);
        const median = sorted.length % 2 === 0
            ? (sorted[mid - 1] + sorted[mid]) / 2
            : sorted[mid];

        // Memory delta
        let memory = null;
        if (this.memoryBefore && this.memoryAfter) {
            memory = {
                before: this.memoryBefore,
                after: this.memoryAfter,
                delta: {
                    heapUsed: this.memoryAfter.heapUsed - this.memoryBefore.heapUsed,
                    heapTotal: this.memoryAfter.heapTotal - this.memoryBefore.heapTotal,
                    external: this.memoryAfter.external - this.memoryBefore.external,
                    rss: this.memoryAfter.rss - this.memoryBefore.rss
                }
            };
        }

        return {
            name: this.name,
            iterations: this.iterations,
            min: sorted[0],
            max: sorted[sorted.length - 1],
            mean: mean,
            median: median,
            stddev: stddev,
            total: sum,
            memory: memory,
            times: this.times
        };
    }

    // Run benchmark with custom work per iteration
    runWithWork(fn, workPerIteration = 1) {
        this.times = [];
        const timer = BenchmarkRunner.getTimer();
        
        // Warmup
        for (let i = 0; i < this.warmup; i++) {
            fn(workPerIteration);
        }

        this.memoryBefore = BenchmarkRunner.getMemoryUsage();

        // Measurements
        for (let i = 0; i < this.iterations; i++) {
            const start = timer();
            fn(workPerIteration);
            const end = timer();
            this.times.push(end - start);
        }

        this.memoryAfter = BenchmarkRunner.getMemoryUsage();

        const stats = this.getStats();
        // Calculate operations per second
        stats.opsPerSecond = (workPerIteration * this.iterations) / (stats.total / 1000);
        return stats;
    }
}

// Export for use in other benchmark files
if (typeof module !== 'undefined' && typeof module.exports !== 'undefined') {
    module.exports = BenchmarkRunner;
}
