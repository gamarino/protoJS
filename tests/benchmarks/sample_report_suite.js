// Sample Report Generator
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
// HTML Report Generator
// Generates user-friendly HTML reports with charts and comparison tables

function generateHTMLReport(allResults, comparison = null) {
    const timestamp = new Date().toISOString();
    const dateStr = new Date().toLocaleString();
    
    // Get engine versions
    let protojsVersion = 'Unknown';
    let nodejsVersion = null;
    
    try {
        if (typeof process !== 'undefined' && process.versions) {
            if (process.versions.protojs) {
                protojsVersion = process.versions.protojs;
            }
            if (process.versions.node) {
                nodejsVersion = process.versions.node;
            }
        }
    } catch (e) {
        // Ignore
    }

    const html = `<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>ProtoJS Performance Report</title>
    <script src="https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js"></script>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
            line-height: 1.6;
            color: #333;
            background: #f5f5f5;
            padding: 20px;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
            background: white;
            padding: 30px;
            border-radius: 8px;
            box-shadow: 0 2px 10px rgba(0,0,0,0.1);
        }
        
        h1 {
            color: #2c3e50;
            border-bottom: 3px solid #3498db;
            padding-bottom: 10px;
            margin-bottom: 30px;
        }
        
        h2 {
            color: #34495e;
            margin-top: 40px;
            margin-bottom: 20px;
            padding-bottom: 10px;
            border-bottom: 2px solid #ecf0f1;
        }
        
        h3 {
            color: #555;
            margin-top: 30px;
            margin-bottom: 15px;
        }
        
        .summary {
            background: #ecf0f1;
            padding: 20px;
            border-radius: 5px;
            margin-bottom: 30px;
        }
        
        .summary-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin-top: 15px;
        }
        
        .summary-item {
            background: white;
            padding: 15px;
            border-radius: 5px;
            border-left: 4px solid #3498db;
        }
        
        .summary-item h4 {
            color: #7f8c8d;
            font-size: 0.9em;
            margin-bottom: 5px;
        }
        
        .summary-item .value {
            font-size: 1.5em;
            font-weight: bold;
            color: #2c3e50;
        }
        
        table {
            width: 100%;
            border-collapse: collapse;
            margin: 20px 0;
            background: white;
        }
        
        th, td {
            padding: 12px;
            text-align: left;
            border-bottom: 1px solid #ddd;
        }
        
        th {
            background: #34495e;
            color: white;
            font-weight: 600;
            cursor: pointer;
            user-select: none;
        }
        
        th:hover {
            background: #2c3e50;
        }
        
        tr:hover {
            background: #f8f9fa;
        }
        
        .faster {
            color: #27ae60;
            font-weight: bold;
        }
        
        .slower {
            color: #e74c3c;
            font-weight: bold;
        }
        
        .similar {
            color: #f39c12;
            font-weight: bold;
        }
        
        .chart-container {
            margin: 30px 0;
            padding: 20px;
            background: #fafafa;
            border-radius: 5px;
        }
        
        .section {
            margin-bottom: 40px;
        }
        
        .footer {
            margin-top: 50px;
            padding-top: 20px;
            border-top: 2px solid #ecf0f1;
            color: #7f8c8d;
            font-size: 0.9em;
        }
        
        .badge {
            display: inline-block;
            padding: 4px 8px;
            border-radius: 3px;
            font-size: 0.85em;
            font-weight: bold;
        }
        
        .badge-success {
            background: #27ae60;
            color: white;
        }
        
        .badge-warning {
            background: #f39c12;
            color: white;
        }
        
        .badge-danger {
            background: #e74c3c;
            color: white;
        }
        
        .expandable {
            cursor: pointer;
        }
        
        .expandable::before {
            content: '▶ ';
            display: inline-block;
            transition: transform 0.2s;
        }
        
        .expandable.expanded::before {
            transform: rotate(90deg);
        }
        
        .hidden {
            display: none;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>ProtoJS Performance Report</h1>
        
        <div class="summary">
            <h2>Executive Summary</h2>
            <p><strong>Generated:</strong> ${dateStr}</p>
            <p><strong>ProtoJS Version:</strong> ${protojsVersion}</p>
            ${nodejsVersion ? `<p><strong>Node.js Version:</strong> ${nodejsVersion}</p>` : ''}
            
            <div class="summary-grid">
                ${generateSummaryStats(allResults, comparison)}
            </div>
        </div>
        
        ${generateCategorySections(allResults, comparison)}
        
        <div class="footer">
            <p><strong>Test Environment:</strong> ${getEnvironmentInfo()}</p>
            <p><strong>Methodology:</strong> All benchmarks run with 100 iterations (default) and 10 warmup iterations. Results show mean execution time in milliseconds unless otherwise specified.</p>
        </div>
    </div>
    
    <script>
        ${generateChartScripts(allResults, comparison)}
        
        // Table sorting functionality
        document.querySelectorAll('th').forEach(header => {
            header.addEventListener('click', () => {
                const table = header.closest('table');
                const tbody = table.querySelector('tbody');
                const rows = Array.from(tbody.querySelectorAll('tr'));
                const index = Array.from(header.parentElement.children).indexOf(header);
                const isAsc = header.classList.contains('asc');
                
                rows.sort((a, b) => {
                    const aVal = a.children[index].textContent.trim();
                    const bVal = b.children[index].textContent.trim();
                    const aNum = parseFloat(aVal);
                    const bNum = parseFloat(bVal);
                    
                    if (!isNaN(aNum) && !isNaN(bNum)) {
                        return isAsc ? bNum - aNum : aNum - bNum;
                    }
                    return isAsc ? bVal.localeCompare(aVal) : aVal.localeCompare(bVal);
                });
                
                rows.forEach(row => tbody.appendChild(row));
                header.classList.toggle('asc');
            });
        });
        
        // Expandable sections
        document.querySelectorAll('.expandable').forEach(el => {
            el.addEventListener('click', () => {
                el.classList.toggle('expanded');
                const content = el.nextElementSibling;
                if (content) {
                    content.classList.toggle('hidden');
                }
            });
        });
    </script>
</body>
</html>`;

    return html;
}

function generateSummaryStats(allResults, comparison) {
    let totalTests = 0;
    let totalCategories = 0;
    
    allResults.forEach(category => {
        totalCategories++;
        if (category.tests) {
            totalTests += category.tests.length;
        }
    });
    
    let comparedTests = 0;
    if (comparison && comparison.summary) {
        comparedTests = comparison.summary.compared || 0;
    }
    
    return `
        <div class="summary-item">
            <h4>Total Categories</h4>
            <div class="value">${totalCategories}</div>
        </div>
        <div class="summary-item">
            <h4>Total Tests</h4>
            <div class="value">${totalTests}</div>
        </div>
        ${comparedTests > 0 ? `
        <div class="summary-item">
            <h4>Compared Tests</h4>
            <div class="value">${comparedTests}</div>
        </div>
        ` : ''}
    `;
}

function generateCategorySections(allResults, comparison) {
    let html = '';
    
    allResults.forEach(category => {
        html += `
        <div class="section">
            <h2>${category.category}</h2>
            ${generateTestTable(category, comparison)}
            ${generateCategoryChart(category, comparison)}
        </div>
        `;
    });
    
    return html;
}

function generateTestTable(category, comparison) {
    if (!category.tests || category.tests.length === 0) {
        return '<p>No tests available for this category.</p>';
    }
    
    // Create comparison map
    const comparisonMap = {};
    if (comparison && comparison.tests) {
        comparison.tests.forEach(test => {
            if (test.category === category.category) {
                comparisonMap[test.name] = test;
            }
        });
    }
    
    let tableHtml = `
    <table>
        <thead>
            <tr>
                <th>Test Name</th>
                <th>Mean (ms)</th>
                <th>Median (ms)</th>
                <th>Min (ms)</th>
                <th>Max (ms)</th>
                <th>Std Dev (ms)</th>
                ${comparison ? '<th>Comparison</th>' : ''}
            </tr>
        </thead>
        <tbody>
    `;
    
    category.tests.forEach(test => {
        const comp = comparisonMap[test.name];
        let comparisonCell = '';
        
        if (comp && comp.comparison) {
            const { ratio, percentDiff, faster } = comp.comparison;
            const badgeClass = faster === 'protojs' ? 'badge-success' : 
                             Math.abs(percentDiff) < 10 ? 'badge-warning' : 'badge-danger';
            const badgeText = faster === 'protojs' ? 'Faster' : 
                            Math.abs(percentDiff) < 10 ? 'Similar' : 'Slower';
            comparisonCell = `
                <td>
                    <span class="badge ${badgeClass}">${badgeText}</span><br>
                    <small>${percentDiff > 0 ? '+' : ''}${percentDiff.toFixed(1)}%</small>
                </td>
            `;
        } else if (comparison) {
            comparisonCell = '<td>-</td>';
        }
        
        tableHtml += `
            <tr>
                <td>${test.name}</td>
                <td>${test.mean.toFixed(3)}</td>
                <td>${test.median.toFixed(3)}</td>
                <td>${test.min.toFixed(3)}</td>
                <td>${test.max.toFixed(3)}</td>
                <td>${test.stddev.toFixed(3)}</td>
                ${comparisonCell}
            </tr>
        `;
    });
    
    tableHtml += `
        </tbody>
    </table>
    `;
    
    return tableHtml;
}

function generateCategoryChart(category, comparison) {
    if (!category.tests || category.tests.length === 0) {
        return '';
    }
    
    const chartId = `chart-${category.category.replace(/\s+/g, '-').toLowerCase()}`;
    const labels = category.tests.map(t => t.name);
    const protojsData = category.tests.map(t => t.mean);
    
    let nodejsData = null;
    if (comparison && comparison.tests) {
        const comparisonMap = {};
        comparison.tests.forEach(test => {
            if (test.category === category.category && test.nodejs) {
                comparisonMap[test.name] = test.nodejs.mean;
            }
        });
        nodejsData = category.tests.map(t => comparisonMap[t.name] || null);
    }
    
    const datasets = [{
        label: 'ProtoJS',
        data: protojsData,
        backgroundColor: 'rgba(52, 152, 219, 0.6)',
        borderColor: 'rgba(52, 152, 219, 1)',
        borderWidth: 1
    }];
    
    if (nodejsData && nodejsData.some(v => v !== null)) {
        datasets.push({
            label: 'Node.js',
            data: nodejsData,
            backgroundColor: 'rgba(46, 204, 113, 0.6)',
            borderColor: 'rgba(46, 204, 113, 1)',
            borderWidth: 1
        });
    }
    
    return `
    <div class="chart-container">
        <h3>Performance Comparison Chart</h3>
        <canvas id="${chartId}" width="400" height="200"></canvas>
        <script>
            (function() {
                const ctx = document.getElementById('${chartId}').getContext('2d');
                new Chart(ctx, {
                    type: 'bar',
                    data: {
                        labels: ${JSON.stringify(labels)},
                        datasets: ${JSON.stringify(datasets)}
                    },
                    options: {
                        responsive: true,
                        scales: {
                            y: {
                                beginAtZero: true,
                                title: {
                                    display: true,
                                    text: 'Time (ms)'
                                }
                            }
                        }
                    }
                });
            })();
        </script>
    </div>
    `;
}

function generateChartScripts(allResults, comparison) {
    // Charts are generated inline in generateCategoryChart
    return '';
}

function getEnvironmentInfo() {
    let info = [];
    
    if (typeof process !== 'undefined') {
        if (process.platform) info.push(`Platform: ${process.platform}`);
        if (process.arch) info.push(`Architecture: ${process.arch}`);
    }
    
    return info.length > 0 ? info.join(', ') : 'Unknown';
}

// Export for use in main benchmark runner
if (typeof module !== 'undefined' && typeof module.exports !== 'undefined') {
    module.exports = {
        generateHTMLReport,
        generateSummaryStats,
        generateCategorySections,
        generateTestTable,
        generateCategoryChart
    };
}
// Generate Sample Performance Report for Documentation
// Creates a representative report with sample data

// Load report generator
// (Assuming report_generator.js functions are available)

// Create sample results
const sampleResults = [
    {
        category: 'Basic Types',
        tests: [
            {
                name: 'Number Addition',
                mean: 12.5,
                median: 12.3,
                min: 11.8,
                max: 13.2,
                stddev: 0.4,
                iterations: 100
            },
            {
                name: 'String Concatenation',
                mean: 45.2,
                median: 44.8,
                min: 43.1,
                max: 47.5,
                stddev: 1.2,
                iterations: 100
            },
            {
                name: 'Boolean Operations',
                mean: 8.3,
                median: 8.2,
                min: 7.9,
                max: 8.7,
                stddev: 0.2,
                iterations: 100
            }
        ]
    },
    {
        category: 'Collections',
        tests: [
            {
                name: 'Array Creation',
                mean: 125.4,
                median: 124.8,
                min: 122.1,
                max: 128.9,
                stddev: 2.1,
                iterations: 50
            },
            {
                name: 'Array Map',
                mean: 89.6,
                median: 89.2,
                min: 87.5,
                max: 91.8,
                stddev: 1.3,
                iterations: 20
            },
            {
                name: 'Object Property Access',
                mean: 15.2,
                median: 15.1,
                min: 14.8,
                max: 15.6,
                stddev: 0.3,
                iterations: 100
            }
        ]
    },
    {
        category: 'Overall Performance',
        tests: [
            {
                name: 'Startup Time',
                mean: 520.5,
                median: 518.2,
                min: 510.1,
                max: 535.8,
                stddev: 8.2,
                iterations: 10
            },
            {
                name: 'Throughput (Simple Ops)',
                mean: 0.0012,
                median: 0.0011,
                min: 0.0010,
                max: 0.0015,
                stddev: 0.0001,
                iterations: 10,
                opsPerSecond: 833333
            }
        ]
    }
];

console.log('Generating sample performance report...\n');

if (typeof generateHTMLReport === 'function') {
    const htmlReport = generateHTMLReport(sampleResults, null);
    
    // Output to console (will be captured)
    console.log('=== HTML REPORT START ===');
    console.log(htmlReport);
    console.log('=== HTML REPORT END ===');
    
    // Also store in global
    if (typeof globalThis !== 'undefined') {
        globalThis.performanceReport = htmlReport;
    }
    
    console.log('\nSample report generated successfully!');
    console.log(`Report length: ${htmlReport.length} characters`);
} else {
    console.error('generateHTMLReport function not found');
    console.log('Sample results:', JSON.stringify(sampleResults, null, 2));
}
