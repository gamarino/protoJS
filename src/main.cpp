#include "JSContext.h"
#include "Deferred.h"
#include "EventLoop.h"
#include "console.h"
#include "modules/IOModule.h"
#include "modules/ProtoCoreModule.h"
#include "modules/ProcessModule.h"
#include "modules/CommonJSLoader.h"
#include "modules/path/PathModule.h"
#include "modules/fs/FSModule.h"
#include "modules/url/URLModule.h"
#include "modules/http/HTTPModule.h"
#include "modules/events/EventsModule.h"
#include "modules/stream/StreamModule.h"
#include "modules/util/UtilModule.h"
#include "modules/crypto/CryptoModule.h"
#include "modules/buffer/BufferModule.h"
#include "modules/net/NetModule.h"
#include "modules/worker_threads/WorkerThreadsModule.h"
#include "modules/cluster/ClusterModule.h"
#include "modules/dgram/DgramModule.h"
#include "modules/child_process/ChildProcessModule.h"
#include "modules/dns/DNSModule.h"
#include "profiling/Profiler.h"
#include "profiling/VisualProfiler.h"
#include "memory/MemoryAnalyzer.h"
#include "debugging/IntegratedDebugger.h"
#include "repl/REPL.h"
#include "quickjs.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " [options] <filename.js> or " << programName << " -e \"code\"" << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --cpu-threads N      Number of CPU threads (default: number of CPU cores)" << std::endl;
    std::cerr << "  --io-threads N       Number of I/O threads (default: 3-4x CPU cores)" << std::endl;
    std::cerr << "  --io-threads-factor F  Multiplier for I/O threads (default: 3.0)" << std::endl;
    std::cerr << "  -e \"code\"            Execute code directly" << std::endl;
    std::cerr << "  -p, --print          Print result of -e" << std::endl;
    std::cerr << "  -c, --check          Syntax check only" << std::endl;
    std::cerr << "  -v, --version        Show version" << std::endl;
    std::cerr << "  --input-type=module  Treat input as ES module" << std::endl;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    size_t cpuThreads = 0;
    size_t ioThreads = 0;
    double ioFactor = 3.0;
    std::string code;
    std::string filename = "eval";
    bool executeCode = false;
    bool printResult = false;
    bool syntaxCheck = false;
    bool showVersion = false;
    bool inputTypeModule = false;

    // Parse arguments
    int i = 1;
    while (i < argc) {
        std::string arg = argv[i];
        
        if (arg == "--cpu-threads" && i + 1 < argc) {
            cpuThreads = std::stoul(argv[++i]);
        } else if (arg == "--io-threads" && i + 1 < argc) {
            ioThreads = std::stoul(argv[++i]);
        } else if (arg == "--io-threads-factor" && i + 1 < argc) {
            ioFactor = std::stod(argv[++i]);
        } else if (arg == "-e" && i + 1 < argc) {
            executeCode = true;
            code = argv[++i];
        } else if (arg == "-p" || arg == "--print") {
            printResult = true;
        } else if (arg == "-c" || arg == "--check") {
            syntaxCheck = true;
        } else if (arg == "-v" || arg == "--version") {
            showVersion = true;
        } else if (arg == "--input-type=module") {
            inputTypeModule = true;
        } else if (arg[0] != '-') {
            filename = arg;
            std::ifstream file(filename);
            if (!file.is_open()) {
                std::cerr << "Could not open file: " << filename << std::endl;
                return 1;
            }
            std::stringstream ss;
            ss << file.rdbuf();
            code = ss.str();
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        i++;
    }

    // Handle version flag
    if (showVersion) {
        std::cout << "protoJS v0.1.0" << std::endl;
        return 0;
    }

    // If no file and no -e, start REPL
    if (code.empty() && !executeCode && !syntaxCheck) {
        protojs::JSContextWrapper wrapper(cpuThreads, ioThreads, ioFactor);
        
        // Initialize all modules for REPL
        protojs::Console::init(wrapper.getJSContext());
        protojs::Deferred::init(wrapper.getJSContext(), &wrapper);
        protojs::IOModule::init(wrapper.getJSContext());
        protojs::ProtoCoreModule::init(wrapper.getJSContext());
        protojs::ProcessModule::init(wrapper.getJSContext(), argc, argv);
        protojs::CommonJSLoader::init(wrapper.getJSContext());
        protojs::PathModule::init(wrapper.getJSContext());
        protojs::FSModule::init(wrapper.getJSContext());
        protojs::URLModule::init(wrapper.getJSContext());
        protojs::HTTPModule::init(wrapper.getJSContext());
        protojs::EventsModule::init(wrapper.getJSContext());
        protojs::StreamModule::init(wrapper.getJSContext());
        protojs::UtilModule::init(wrapper.getJSContext());
        protojs::CryptoModule::init(wrapper.getJSContext());
        protojs::BufferModule::init(wrapper.getJSContext());
        protojs::NetModule::init(wrapper.getJSContext());
        protojs::WorkerThreadsModule::init(wrapper.getJSContext());
        protojs::ClusterModule::init(wrapper.getJSContext());
        protojs::DgramModule::init(wrapper.getJSContext());
        protojs::ChildProcessModule::init(wrapper.getJSContext());
    protojs::DNSModule::init(wrapper.getJSContext());
    protojs::ChildProcessModule::init(wrapper.getJSContext());
    protojs::MemoryAnalyzer::init(wrapper.getJSContext());
    protojs::Profiler::init(wrapper.getJSContext());
    protojs::VisualProfiler::init(wrapper.getJSContext());
    protojs::IntegratedDebugger::init(wrapper.getJSContext());
        
        protojs::REPL::start(wrapper.getJSContext());
        return 0;
    }

    if (code.empty() && !executeCode) {
        std::cerr << "No code to execute" << std::endl;
        printUsage(argv[0]);
        return 1;
    }

    // Create wrapper with thread pool configuration
    protojs::JSContextWrapper wrapper(cpuThreads, ioThreads, ioFactor);
    
    // Initialize modules
    protojs::Console::init(wrapper.getJSContext());
    protojs::Deferred::init(wrapper.getJSContext(), &wrapper);
    protojs::IOModule::init(wrapper.getJSContext());
    protojs::ProtoCoreModule::init(wrapper.getJSContext());
    protojs::ProcessModule::init(wrapper.getJSContext(), argc, argv);
    
    // Initialize Phase 2 modules
    protojs::CommonJSLoader::init(wrapper.getJSContext());
    // ES Module loader will be used via import statements
    protojs::PathModule::init(wrapper.getJSContext());
    protojs::FSModule::init(wrapper.getJSContext());
    protojs::URLModule::init(wrapper.getJSContext());
    protojs::HTTPModule::init(wrapper.getJSContext());
    protojs::EventsModule::init(wrapper.getJSContext());
    protojs::StreamModule::init(wrapper.getJSContext());
    protojs::UtilModule::init(wrapper.getJSContext());
    protojs::CryptoModule::init(wrapper.getJSContext());
    protojs::BufferModule::init(wrapper.getJSContext());
    protojs::NetModule::init(wrapper.getJSContext());
    protojs::WorkerThreadsModule::init(wrapper.getJSContext());
    protojs::ClusterModule::init(wrapper.getJSContext());
    protojs::DgramModule::init(wrapper.getJSContext());
    protojs::ChildProcessModule::init(wrapper.getJSContext());
    protojs::DNSModule::init(wrapper.getJSContext());
    protojs::MemoryAnalyzer::init(wrapper.getJSContext());
    protojs::Profiler::init(wrapper.getJSContext());
    protojs::VisualProfiler::init(wrapper.getJSContext());
    protojs::IntegratedDebugger::init(wrapper.getJSContext());

    // Set __filename and __dirname for main script so require() resolves relative to script dir
    {
        JSContext* ctx = wrapper.getJSContext();
        JSValue global = JS_GetGlobalObject(ctx);
        JS_SetPropertyStr(ctx, global, "__filename", JS_NewString(ctx, filename.c_str()));
        size_t lastSlash = filename.find_last_of("/\\");
        std::string dirname = (lastSlash != std::string::npos) ? filename.substr(0, lastSlash) : ".";
        JS_SetPropertyStr(ctx, global, "__dirname", JS_NewString(ctx, dirname.c_str()));
        JS_FreeValue(ctx, global);
    }

    // Handle syntax check
    if (syntaxCheck) {
        // For syntax check, we'd parse without executing
        // QuickJS doesn't have a separate parse API, so we'll just try to compile
        JSValue result = wrapper.eval(code, filename);
        if (JS_IsException(result)) {
            JSValue exception = JS_GetException(wrapper.getJSContext());
            const char* error = JS_ToCString(wrapper.getJSContext(), exception);
            if (error) {
                std::cerr << "Syntax Error: " << error << std::endl;
                JS_FreeCString(wrapper.getJSContext(), error);
            }
            JS_FreeValue(wrapper.getJSContext(), exception);
            JS_FreeValue(wrapper.getJSContext(), result);
            return 1;
        }
        JS_FreeValue(wrapper.getJSContext(), result);
        return 0;
    }
    
    // Evaluate code
    JSValue result = wrapper.eval(code, filename);
    
    // Print result if -p flag is set
    if (printResult && !JS_IsException(result) && !JS_IsUndefined(result)) {
        const char* resultStr = JS_ToCString(wrapper.getJSContext(), result);
        if (resultStr) {
            std::cout << resultStr << std::endl;
            JS_FreeCString(wrapper.getJSContext(), resultStr);
        }
    }
    
    // Process event loop to handle any Deferred callbacks
    // Wait for all callbacks to complete (with timeout)
    auto start = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(30); // 30 second timeout
    
    while (protojs::EventLoop::getInstance().hasPendingCallbacks()) {
        protojs::EventLoop::getInstance().processCallbacks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Check timeout
        auto now = std::chrono::steady_clock::now();
        if (now - start > timeout) {
            std::cerr << "Warning: Event loop timeout reached. Some callbacks may not have completed." << std::endl;
            break;
        }
    }
    
    // Process any remaining callbacks one more time
    protojs::EventLoop::getInstance().processCallbacks();
    
    JS_FreeValue(wrapper.getJSContext(), result);

    return 0;
}
