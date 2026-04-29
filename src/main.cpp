#include "JSContext.h"
#include "headers/protoCore.h"
#include "Deferred.h"
#include "ProtoDeferred.h"
#include "ProtoCoreNativeBindings.h"
#include "EventLoop.h"
#include "EventLoopBindings.h"
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
#include <vector>

// JSON.stringify / JSON.parse polyfill, prepended to user code in the
// protoCore eval path.  ProtoInterpreter installs an empty `JSON` stub
// on the protoCore-side global; QuickJS's native JSON is not plumbed
// through to it, so scripts would see `JSON.stringify === undefined`
// without this shim.  Prepended (not eval'd separately) because
// cross-wrapper.eval function references currently don't work — the
// function's bytecode is module-relative, so installer-defined
// helpers go stale once their installer module's tables are released.
//
// Expects `this` to be the protoCore-side global at top-level eval,
// which is true for the standard CLI (full-init) path.
#include "JSONBuiltin.h"


// setImmediate now lives entirely on the protoCore side (see
// src/EventLoopBindings.cpp).  The QuickJS-side js_setImmediate that used
// to live here installed via JS_SetPropertyStr on the QuickJS global, which
// did not propagate to the protoCore-native global; user code observed
// `typeof setImmediate === 'undefined'`.

// Install the per-script primitive globals (__filename, __dirname,
// __protojs__) on the protoCore-native global.  Returns the (possibly
// new) global pointer; caller persists it via wrapper.updateNativeGlobal.
static const proto::ProtoObject* installScriptGlobals(
    proto::ProtoContext* pCtx,
    const proto::ProtoObject* g,
    const std::string& filename) {
    if (!pCtx || !g) return g;
    auto setStr = [&](const char* name, const std::string& value) {
        const proto::ProtoString* k = pCtx->fromUTF8String(name)
            ? pCtx->fromUTF8String(name)->asString(pCtx) : nullptr;
        if (!k) return;
        g = g->setAttribute(pCtx, k, pCtx->fromUTF8String(value.c_str()));
    };
    setStr("__filename", filename);
    size_t lastSlash = filename.find_last_of("/\\");
    setStr("__dirname",
           (lastSlash != std::string::npos) ? filename.substr(0, lastSlash) : ".");
    const proto::ProtoString* pjKey = pCtx->fromUTF8String("__protojs__")
        ? pCtx->fromUTF8String("__protojs__")->asString(pCtx) : nullptr;
    if (pjKey) g = g->setAttribute(pCtx, pjKey, PROTO_TRUE);
    return g;
}

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
    std::cerr << "  --proto-eval         Use protoCore interpreter for eval (compile-only + ProtoInterpreter)" << std::endl;
    std::cerr << "  --minimal            Minimal init (Console only); use to isolate compile/run issues" << std::endl;
    std::cerr << "  --preload file.js    Evaluate file as script before main module (sets globals)" << std::endl;
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
    bool useProtoEvalCli = false;
    bool minimalInit = false;
    std::vector<std::string> preloadFiles;

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
        } else if (arg == "--proto-eval") {
            useProtoEvalCli = true;
        } else if (arg == "--minimal") {
            minimalInit = true;
        } else if (arg == "--preload" && i + 1 < argc) {
            preloadFiles.push_back(argv[++i]);
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

        // protoCore is the single execution path (compile → load → run).
        wrapper.setUseProtoEval(true);

        // Initialize all modules for REPL
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            protojs::Console::init(wrapper.getProtoContext(), nativeGlobal);
            protojs::JSONBuiltin::init(wrapper.getProtoContext(), nativeGlobal);
            protojs::TimingAPIs::init(wrapper.getProtoContext(), nativeGlobal);
            nativeGlobal = protojs::EventLoopBindings::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        protojs::Deferred::init(wrapper.getJSContext(), &wrapper);
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::IOModule::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        protojs::ProtoCoreModule::init(wrapper.getJSContext());
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::ProcessModule::init(
                wrapper.getProtoContext(), nativeGlobal, argc, argv);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::CommonJSLoader::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::PathModule::init(
                wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::FSModule::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::URLModule::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::HTTPModule::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::EventsModule::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::StreamModule::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::UtilModule::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::CryptoModule::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::BufferModule::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::NetModule::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::WorkerThreadsModule::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::ClusterModule::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::DgramModule::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            nativeGlobal = protojs::ChildProcessModule::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::DNSModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::ChildProcessModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::MemoryAnalyzer::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::Profiler::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::VisualProfiler::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::IntegratedDebugger::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
        
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

    // protoCore is the single execution path (compile → load → run).
    wrapper.setUseProtoEval(true);

    if (minimalInit) {
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            protojs::Console::init(wrapper.getProtoContext(), nativeGlobal);
            protojs::JSONBuiltin::init(wrapper.getProtoContext(), nativeGlobal);
            protojs::TimingAPIs::init(wrapper.getProtoContext(), nativeGlobal);
            nativeGlobal = protojs::ProtoDeferred::init(wrapper.getProtoContext(), nativeGlobal);
            nativeGlobal = protojs::ProtoCoreNativeBindings::init(wrapper.getProtoContext(), nativeGlobal);
            nativeGlobal = installScriptGlobals(wrapper.getProtoContext(), nativeGlobal, filename);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        JSValue result = wrapper.eval(code, filename, inputTypeModule);
        JS_FreeValue(wrapper.getJSContext(), result);
        return 0;
    }

    // Initialize modules
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        protojs::Console::init(wrapper.getProtoContext(), nativeGlobal);
        protojs::JSONBuiltin::init(wrapper.getProtoContext(), nativeGlobal);
        protojs::TimingAPIs::init(wrapper.getProtoContext(), nativeGlobal);
        nativeGlobal = protojs::EventLoopBindings::init(wrapper.getProtoContext(), nativeGlobal);
        nativeGlobal = protojs::ProtoDeferred::init(wrapper.getProtoContext(), nativeGlobal);
        nativeGlobal = protojs::ProtoCoreNativeBindings::init(wrapper.getProtoContext(), nativeGlobal);
        nativeGlobal = installScriptGlobals(wrapper.getProtoContext(), nativeGlobal, filename);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    // (__protojs__ is now set by installScriptGlobals above on the
    // protoCore-native global.)

    // JSON.stringify / JSON.parse polyfill is prepended to the user's
    // code below (see line ~392) rather than eval'd separately.  In the
    // current runtime, function references defined in one wrapper.eval
    // call do not work correctly when called from a later one (the
    // function's bytecode is keyed by module-relative bcId, which goes
    // stale once that module's compile-time tables are released).
    // Prepending keeps the polyfill and user code in the same module
    // so the references stay valid.
    protojs::Deferred::init(wrapper.getJSContext(), &wrapper);
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::IOModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    protojs::ProtoCoreModule::init(wrapper.getJSContext());
    // ProcessModule registers `process` directly on the protoCore-native
    // global (no QuickJS bridge).  The hang that affected the previous
    // QuickJS-side install path is gone with the migration.
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::ProcessModule::init(
            wrapper.getProtoContext(), nativeGlobal, argc, argv);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    
    // Initialize Phase 2 modules
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::CommonJSLoader::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    // ES Module loader will be used via import statements
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::PathModule::init(
            wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::FSModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::URLModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::HTTPModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::EventsModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::StreamModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::UtilModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::CryptoModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::BufferModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::NetModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::WorkerThreadsModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::ClusterModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::DgramModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::ChildProcessModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::DNSModule::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::MemoryAnalyzer::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::Profiler::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::VisualProfiler::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        nativeGlobal = protojs::IntegratedDebugger::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }

    // __filename / __dirname / __protojs__ are now set by installScriptGlobals
    // earlier on the protoCore-native global.  No QuickJS-side install here.

    // Handle syntax check
    if (syntaxCheck) {
        // For syntax check, we'd parse without executing
        // QuickJS doesn't have a separate parse API, so we'll just try to compile
        JSValue result = wrapper.eval(code, filename, inputTypeModule);
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
    
    // Evaluate preload files as scripts to set up globals (e.g., harness for test262).
    for (const auto& preload : preloadFiles) {
        std::ifstream pf(preload);
        if (!pf.is_open()) {
            std::cerr << "Could not open preload file: " << preload << std::endl;
            return 1;
        }
        std::stringstream pss;
        pss << pf.rdbuf();
        JSValue pResult = wrapper.evalPreload(pss.str(), preload);
        if (JS_IsException(pResult)) {
            JS_FreeValue(wrapper.getJSContext(), pResult);
            return 1;
        }
        JS_FreeValue(wrapper.getJSContext(), pResult);
    }

    JSValue result = wrapper.eval(code, filename, inputTypeModule);


    // Print result if -p flag is set
    if (printResult && !JS_IsException(result) && !JS_IsUndefined(result)) {
        const char* resultStr = JS_ToCString(wrapper.getJSContext(), result);
        if (resultStr) {
            std::cout << resultStr << std::endl;
            JS_FreeCString(wrapper.getJSContext(), resultStr);
        }
    }
    
    // Process event loop: handle Deferred/Worker callbacks and wait for worker threads
    auto start = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::seconds(180);  // Allow parallel_cpu (5 rounds × 4 staggered ProtoThreads, 2e6 iter each) to complete

    while (protojs::EventLoop::getInstance().hasPendingCallbacks() ||
           protojs::WorkerThreadsModule::getActiveWorkerCount() > 0 ||
           protojs::Deferred::getActiveDeferredCount() > 0 ||
           protojs::ProtoDeferred::getActiveCount() > 0 ||
           protojs::HTTPModule::getActiveServerCount() > 0 ||
           protojs::HTTPModule::getActiveClientCount() > 0 ||
           protojs::NetModule::getActiveCount() > 0) {
        protojs::EventLoop::getInstance().processCallbacks();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        auto now = std::chrono::steady_clock::now();
        if (now - start > timeout) {
            std::cerr << "Warning: Event loop timeout reached. Some callbacks may not have completed." << std::endl;
            break;
        }
    }
    
    // Process any remaining callbacks one more time
    protojs::EventLoop::getInstance().processCallbacks();
    
    const int exitCode = JS_IsException(result) ? 1 : 0;
    JS_FreeValue(wrapper.getJSContext(), result);

    return exitCode;
}
