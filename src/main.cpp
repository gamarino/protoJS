#include "JSContext.h"
#include "headers/protoCore.h"
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

static JSValue js_setImmediate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    (void)this_val;
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) return JS_UNDEFINED;
    JSValue cb = JS_DupValue(ctx, argv[0]);
    protojs::EventLoop::getInstance().enqueueCallback([ctx, cb]() {
        JS_Call(ctx, cb, JS_UNDEFINED, 0, nullptr);
        JS_FreeValue(ctx, cb);
    });
    return JS_UNDEFINED;
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
        std::cerr << "[protojs] CLI: creating JSContextWrapper for REPL" << std::endl;
        protojs::JSContextWrapper wrapper(cpuThreads, ioThreads, ioFactor);

        // protoCore is the single execution path (compile → load → run).
        wrapper.setUseProtoEval(true);

        // Initialize all modules for REPL
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            protojs::Console::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
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
    std::cerr << "[protojs] CLI: creating JSContextWrapper for script" << std::endl;
    protojs::JSContextWrapper wrapper(cpuThreads, ioThreads, ioFactor);

    // protoCore is the single execution path (compile → load → run).
    wrapper.setUseProtoEval(true);

    if (minimalInit) {
        std::cerr << "[protojs] CLI: minimal init (Console only)" << std::endl;
        {
            const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
            protojs::Console::init(wrapper.getProtoContext(), nativeGlobal);
            wrapper.updateNativeGlobal(nativeGlobal);
        }
        {
            JSContext* ctx = wrapper.getJSContext();
            JSValue global = JS_GetGlobalObject(ctx);
            JS_SetPropertyStr(ctx, global, "__protojs__", JS_NewBool(ctx, 1));
            JS_SetPropertyStr(ctx, global, "__filename", JS_NewString(ctx, filename.c_str()));
            size_t lastSlash = filename.find_last_of("/\\");
            std::string dirname = (lastSlash != std::string::npos) ? filename.substr(0, lastSlash) : ".";
            JS_SetPropertyStr(ctx, global, "__dirname", JS_NewString(ctx, dirname.c_str()));
            JS_FreeValue(ctx, global);
        }
        JSValue result = wrapper.eval(code, filename, inputTypeModule);
        JS_FreeValue(wrapper.getJSContext(), result);
        return 0;
    }

    // Initialize modules
    std::cerr << "[protojs] CLI: initializing core modules" << std::endl;
    {
        const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
        protojs::Console::init(wrapper.getProtoContext(), nativeGlobal);
        wrapper.updateNativeGlobal(nativeGlobal);
    }
    std::cerr << "[protojs] CLI: Console initialized" << std::endl;
    {
        JSContext* ctx = wrapper.getJSContext();
        JSValue global = JS_GetGlobalObject(ctx);
        JS_SetPropertyStr(ctx, global, "__protojs__", JS_NewBool(ctx, 1));
        JS_FreeValue(ctx, global);
    }
    protojs::Deferred::init(wrapper.getJSContext(), &wrapper);
    std::cerr << "[protojs] CLI: Deferred initialized" << std::endl;
    protojs::IOModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: IOModule initialized" << std::endl;
    protojs::ProtoCoreModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: ProtoCoreModule initialized" << std::endl;
    // NOTE: ProcessModule::init currently triggers a hang in the CLI path
    // under the new protoCore runtime wiring. Skip it for now in the basic
    // CLI runner; Node-style process emulation is still available via
    // higher-level test harnesses.
    // protojs::ProcessModule::init(wrapper.getJSContext(), argc, argv);
    std::cerr << "[protojs] CLI: ProcessModule init skipped in CLI path" << std::endl;
    
    // Initialize Phase 2 modules
    protojs::CommonJSLoader::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: CommonJSLoader initialized" << std::endl;
    // ES Module loader will be used via import statements
    protojs::PathModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: PathModule initialized" << std::endl;
    protojs::FSModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: FSModule initialized" << std::endl;
    protojs::URLModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: URLModule initialized" << std::endl;
    protojs::HTTPModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: HTTPModule initialized" << std::endl;
    protojs::EventsModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: EventsModule initialized" << std::endl;
    protojs::StreamModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: StreamModule initialized" << std::endl;
    protojs::UtilModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: UtilModule initialized" << std::endl;
    protojs::CryptoModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: CryptoModule initialized" << std::endl;
    protojs::BufferModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: BufferModule initialized" << std::endl;
    protojs::NetModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: NetModule initialized" << std::endl;
    protojs::WorkerThreadsModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: WorkerThreadsModule initialized" << std::endl;
    protojs::ClusterModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: ClusterModule initialized" << std::endl;
    protojs::DgramModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: DgramModule initialized" << std::endl;
    protojs::ChildProcessModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: ChildProcessModule initialized" << std::endl;
    protojs::DNSModule::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: DNSModule initialized" << std::endl;
    protojs::MemoryAnalyzer::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: MemoryAnalyzer initialized" << std::endl;
    protojs::Profiler::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: Profiler initialized" << std::endl;
    protojs::VisualProfiler::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: VisualProfiler initialized" << std::endl;
    protojs::IntegratedDebugger::init(wrapper.getJSContext());
    std::cerr << "[protojs] CLI: IntegratedDebugger initialized" << std::endl;

    // Set __filename, __dirname, and setImmediate for main script
    {
        JSContext* ctx = wrapper.getJSContext();
        JSValue global = JS_GetGlobalObject(ctx);
        JS_SetPropertyStr(ctx, global, "__filename", JS_NewString(ctx, filename.c_str()));
        size_t lastSlash = filename.find_last_of("/\\");
        std::string dirname = (lastSlash != std::string::npos) ? filename.substr(0, lastSlash) : ".";
        JS_SetPropertyStr(ctx, global, "__dirname", JS_NewString(ctx, dirname.c_str()));
        JSValue setImmediateFn = JS_NewCFunction(ctx, js_setImmediate, "setImmediate", 1);
        JS_SetPropertyStr(ctx, global, "setImmediate", setImmediateFn);
        JS_FreeValue(ctx, global);
    }

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
    
    // Evaluate code
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
           protojs::Deferred::getActiveDeferredCount() > 0) {
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
