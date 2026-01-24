// Process module test

console.log("=== Process Module Tests ===");

if (typeof process !== 'undefined') {
    console.log("process.argv length:", process.argv ? process.argv.length : 0);
    
    if (process.env) {
        console.log("process.env available");
        if (process.env.HOME) {
            console.log("HOME:", process.env.HOME);
        }
    }
    
    if (typeof process.cwd === 'function') {
        console.log("Current directory:", process.cwd());
    }
    
    if (typeof process.platform === 'function') {
        console.log("Platform:", process.platform());
    }
    
    if (typeof process.arch === 'function') {
        console.log("Architecture:", process.arch());
    }
} else {
    console.log("process module not available");
}

console.log("=== Process Module Tests Complete ===");
