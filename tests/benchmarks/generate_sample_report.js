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
