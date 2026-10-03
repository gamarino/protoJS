#include "ProtoCoreTypes.h"
#include "JSContext.h"
#include "ArrayPrototype.h"
#include "FunctionPrototype.h"
#include "protoCore.h"
#include "Deferred.h"
#include "ProtoDeferred.h"
#include "ProtoCoreNativeBindings.h"
#include "EventLoop.h"
#include "MicrotaskQueue.h"
#include "GcOrphanQueue.h"
#include "EventLoopBindings.h"
#include "console.h"
#include "modules/IOModule.h"
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
#include "platform/ProcessExit.h"
#include "platform/SizedThread.h"
#include "quickjs.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <thread>
#include <chrono>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <timeapi.h>
#endif

// Windows: the standard streams carry exactly the bytes the program writes, as
// on Linux and macOS (no "\n" -> "\r\n" translation), and a console shows and
// reads them as UTF-8. The process code page is UTF-8 through the manifest
// (src/windows/utf8.manifest), so argv, getenv and paths are UTF-8 too.
//
// The console's code pages belong to the console, not to this process: they
// outlive it and every later program in the same window inherits them. They are
// restored at exit (platform::addExitHook: a normal return from main, exit()
// and process.exit all run it), so cmd.exe is left as protojs found it.
#if defined(_WIN32)
static UINT g_savedConsoleCP = 0;
static UINT g_savedConsoleOutputCP = 0;
static void restoreConsoleCodePages() {
    if (g_savedConsoleCP != 0) SetConsoleCP(g_savedConsoleCP);
    if (g_savedConsoleOutputCP != 0) SetConsoleOutputCP(g_savedConsoleOutputCP);
}
#endif
static void prepareStandardStreams() {
#if defined(_WIN32)
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    // GetConsoleCP returns 0 when the process has no console (redirected, or a
    // service): then there is nothing to change and nothing to restore.
    g_savedConsoleCP = GetConsoleCP();
    g_savedConsoleOutputCP = GetConsoleOutputCP();
    if (g_savedConsoleCP != 0 || g_savedConsoleOutputCP != 0) {
        protojs::platform::addExitHook(restoreConsoleCodePages);
    }
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

// The native stack of every thread that runs JavaScript: the reservation the
// main thread itself gets. Every JavaScript call is a runBytecode frame on the
// native stack, so a thread with less stack than the main thread runs out of it
// at a depth the main thread reaches easily (macOS gives a new thread 512 KiB).
// Windows: the /STACK reservation of protojs.exe (CMakeLists.txt). Elsewhere:
// what protoCore reports for this thread (protoCore 2.9.0 and later), and the
// 8 MiB Linux and macOS give a main thread by default otherwise.
static size_t mainThreadStackBytes() {
#if defined(PROTOJS_MAIN_STACK_BYTES)
    return static_cast<size_t>(PROTOJS_MAIN_STACK_BYTES);
#else
    size_t bytes = 0;
#if defined(PROTOCORE_HAS_CURRENT_THREAD_STACK_BYTES)
    bytes = proto::ProtoSpace::currentThreadStackBytes();
#endif
    const size_t kDefault = static_cast<size_t>(8) * 1024 * 1024;
    return bytes >= kDefault ? bytes : kDefault;
#endif
}

// Threads protoCore creates (ProtoSpace::newThread) and worker_threads' workers
// get the main thread's stack. setThreadStackBytes exists from protoCore 2.8.0
// (honoured on macOS) and is honoured everywhere from 2.9.0; workers do not
// depend on it (src/platform/SizedThread.h).
static void configureThreadStacks() {
    const size_t bytes = mainThreadStackBytes();
    protojs::platform::setJsThreadStackBytes(bytes);
#if defined(PROTOCORE_HAS_THREAD_STACK_BYTES)
    proto::ProtoSpace::setThreadStackBytes(bytes);
#endif
}

// Windows wakes a sleeping thread on the system timer, every 15.6 ms by default,
// so the event loop's 10 ms poll took 15.6 ms or more per turn. While the loop
// waits for work the timer runs at 1 ms (timeBeginPeriod), and it is put back
// as soon as the loop ends, as Microsoft asks of a program that raises it.
struct EventLoopTimerResolution {
#if defined(_WIN32)
    bool raised = false;
    void raise() { if (!raised) raised = timeBeginPeriod(1) == TIMERR_NOERROR; }
    void lower() { if (raised) { timeEndPeriod(1); raised = false; } }
    ~EventLoopTimerResolution() { lower(); }
#else
    void raise() {}
    void lower() {}
#endif
};

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

// Install every runtime global on the protoCore-native global object.
//
// ONE list, shared by the REPL, by `--minimal` and by the script path, so
// that the three cannot drift apart again.  They had already drifted: the
// REPL installed neither `Deferred` nor `protoCore` nor the script globals,
// so `typeof protoCore` was `undefined` at the prompt, and it initialised
// `child_process` twice.
//
// `minimal` installs only the primitives needed to isolate compiler and
// interpreter problems: console, JSON, the timing APIs, Deferred, protoCore
// and the script globals — no event loop bindings and no Node.js-style
// modules.
static void installRuntimeGlobals(protojs::JSContextWrapper& wrapper,
                                  int argc, char** argv,
                                  const std::string& filename,
                                  bool minimal) {
    proto::ProtoContext* pCtx = wrapper.getProtoContext();

    {
        const proto::ProtoObject* g = wrapper.getNativeGlobal();
        protojs::Console::init(pCtx, g);
        protojs::JSONBuiltin::init(pCtx, g);
        protojs::TimingAPIs::init(pCtx, g);
        if (!minimal) g = protojs::EventLoopBindings::init(pCtx, g);
        g = protojs::ProtoDeferred::init(pCtx, g);
        g = protojs::ProtoCoreNativeBindings::init(pCtx, g);
        g = installScriptGlobals(pCtx, g, filename);
        wrapper.updateNativeGlobal(g);
    }

    if (minimal) return;

    protojs::Deferred::init(wrapper.getJSContext(), &wrapper);

    // ProcessModule needs the command line, so it is installed on its own.
    // process.argv is an Array, so Array.prototype must exist first (the
    // interpreter otherwise installs it when the script starts): pre-fix
    // process.argv had no Array methods (`process.argv.slice(2)` threw).
    {
        const proto::ProtoObject* g = wrapper.getNativeGlobal();
        protojs::ensureFunctionPrototype(pCtx, &g);
        protojs::ensureArrayPrototype(pCtx, &g);
        wrapper.updateNativeGlobal(g);
    }
    {
        const proto::ProtoObject* g = wrapper.getNativeGlobal();
        wrapper.updateNativeGlobal(
            protojs::ProcessModule::init(pCtx, g, argc, argv));
    }

    // Every remaining module takes (context, global) and returns the new
    // global.  Each registers on the protoCore-native global; no QuickJS
    // bridge is involved.
    using ModuleInit =
        const proto::ProtoObject* (*)(proto::ProtoContext*, const proto::ProtoObject*);
    static const ModuleInit kModules[] = {
        &protojs::IOModule::init,
        &protojs::CommonJSLoader::init,
        &protojs::PathModule::init,
        &protojs::FSModule::init,
        &protojs::URLModule::init,
        &protojs::HTTPModule::init,
        &protojs::EventsModule::init,
        &protojs::StreamModule::init,
        &protojs::UtilModule::init,
        &protojs::CryptoModule::init,
        &protojs::BufferModule::init,
        &protojs::NetModule::init,
        &protojs::WorkerThreadsModule::init,
        &protojs::ClusterModule::init,
        &protojs::DgramModule::init,
        &protojs::ChildProcessModule::init,
        &protojs::DNSModule::init,
        &protojs::MemoryAnalyzer::init,
        &protojs::Profiler::init,
        &protojs::VisualProfiler::init,
        &protojs::IntegratedDebugger::init,
    };
    for (ModuleInit init : kModules) {
        const proto::ProtoObject* g = wrapper.getNativeGlobal();
        wrapper.updateNativeGlobal(init(pCtx, g));
    }
}

// Parse the input without executing it (`-c` / `--check`).  A bare QuickJS
// runtime is used on purpose: no ProtoSpace, no thread pools and no module
// initialisation are created, so nothing in the input can run.
// JS_EVAL_FLAG_COMPILE_ONLY compiles without linking imports, which matches
// the behaviour of `node --check`.
static int checkSyntaxOnly(const std::string& code,
                           const std::string& filename,
                           bool inputTypeModule) {
    JSRuntime* rt = JS_NewRuntime();
    if (!rt) {
        std::cerr << "protojs: could not create a JavaScript runtime" << std::endl;
        return 1;
    }
    JSContext* ctx = JS_NewContext(rt);
    if (!ctx) {
        JS_FreeRuntime(rt);
        std::cerr << "protojs: could not create a JavaScript context" << std::endl;
        return 1;
    }

    const int flags =
        (inputTypeModule ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL) |
        JS_EVAL_FLAG_COMPILE_ONLY;
    JSValue compiled = JS_Eval(ctx, code.c_str(), code.size(),
                               filename.c_str(), flags);

    int status = 0;
    if (JS_IsException(compiled)) {
        status = 1;
        JSValue exc = JS_GetException(ctx);
        std::string errStr;
        JSValue nameVal = JS_GetPropertyStr(ctx, exc, "name");
        JSValue msgVal = JS_GetPropertyStr(ctx, exc, "message");
        const char* name = (!JS_IsUndefined(nameVal) && !JS_IsException(nameVal))
                           ? JS_ToCString(ctx, nameVal) : nullptr;
        const char* msg = (!JS_IsUndefined(msgVal) && !JS_IsException(msgVal))
                          ? JS_ToCString(ctx, msgVal) : nullptr;
        if (name || msg) {
            errStr = name ? name : "";
            if (msg) errStr += (errStr.empty() ? "" : ": ") + std::string(msg);
        }
        if (name) JS_FreeCString(ctx, name);
        if (msg) JS_FreeCString(ctx, msg);
        JS_FreeValue(ctx, nameVal);
        JS_FreeValue(ctx, msgVal);
        if (errStr.empty()) {
            const char* str = JS_ToCString(ctx, exc);
            if (str) { errStr = str; JS_FreeCString(ctx, str); }
        }
        std::cerr << filename << ": " << errStr << std::endl;
        // The stack carries the line number QuickJS recorded for the error.
        JSValue stackVal = JS_GetPropertyStr(ctx, exc, "stack");
        if (!JS_IsUndefined(stackVal) && !JS_IsException(stackVal)) {
            const char* stack = JS_ToCString(ctx, stackVal);
            if (stack && *stack) std::cerr << stack;
            if (stack) JS_FreeCString(ctx, stack);
        }
        JS_FreeValue(ctx, stackVal);
        JS_FreeValue(ctx, exc);
    }

    JS_FreeValue(ctx, compiled);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return status;
}

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " [options] <filename.js> or " << programName << " -e \"code\"" << std::endl;
    std::cerr << "Options:" << std::endl;
    std::cerr << "  --cpu-threads N      Number of CPU threads (default: number of CPU cores)" << std::endl;
    std::cerr << "  --io-threads N       Number of I/O threads (default: ceil(hardware threads x factor))" << std::endl;
    std::cerr << "  --io-threads-factor F  Multiplier used when --io-threads is not given (default: 3.0)" << std::endl;
    std::cerr << "  -e \"code\"            Execute code directly" << std::endl;
    std::cerr << "  -p, --print          Print result of -e" << std::endl;
    std::cerr << "  -c, --check          Syntax check only (parse without executing)" << std::endl;
    std::cerr << "  -v, --version        Show version" << std::endl;
    std::cerr << "  --input-type=module  Treat input as ES module" << std::endl;
    std::cerr << "  --proto-eval         Deprecated; accepted and ignored (the protoCore interpreter is always used)" << std::endl;
    std::cerr << "  --minimal            Minimal init (Console only); use to isolate compile/run issues" << std::endl;
    std::cerr << "  --preload file.js    Evaluate file as script before main module (sets globals)" << std::endl;
    std::cerr << "  --unhandled-rejections=MODE  throw (default) or strict: report and exit 1; warn: report; none: ignore" << std::endl;
}

int main(int argc, char** argv) {
    prepareStandardStreams();
    configureThreadStacks();
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
    bool minimalInit = false;
    std::vector<std::string> preloadFiles;

    // Parse arguments
    int scriptArgStart = argc;
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
            // Deprecated no-op: the protoCore interpreter is always used.
            // Accepted silently so existing invocations keep working.
        } else if (arg.rfind("--unhandled-rejections=", 0) == 0) {
            // Node's flag: what a promise rejection nobody handled does.
            const std::string mode = arg.substr(sizeof("--unhandled-rejections=") - 1);
            if (mode == "throw") protojs::MicrotaskQueue::setUnhandledRejectionMode(protojs::MicrotaskQueue::kThrow);
            else if (mode == "strict") protojs::MicrotaskQueue::setUnhandledRejectionMode(protojs::MicrotaskQueue::kStrict);
            else if (mode == "warn") protojs::MicrotaskQueue::setUnhandledRejectionMode(protojs::MicrotaskQueue::kWarn);
            else if (mode == "none") protojs::MicrotaskQueue::setUnhandledRejectionMode(protojs::MicrotaskQueue::kNone);
            else {
                std::cerr << "protojs: invalid value for --unhandled-rejections: " << mode
                          << " (expected throw, strict, warn or none)" << std::endl;
                return 9;
            }
        } else if (arg == "--minimal") {
            minimalInit = true;
        } else if (arg == "--preload" && i + 1 < argc) {
            preloadFiles.push_back(argv[++i]);
        } else if (arg[0] != '-') {
            filename = arg;
            std::ifstream file(filename, std::ios::binary);
            if (!file.is_open()) {
                std::cerr << "Could not open file: " << filename << std::endl;
                return 1;
            }
            std::stringstream ss;
            ss << file.rdbuf();
            code = ss.str();
            // As in Node.js, everything after the script name belongs to the
            // script (process.argv), not to protojs.
            scriptArgStart = i + 1;
            break;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        i++;
    }

    // process.argv as Node.js builds it: the executable, the script (when
    // one was given) and the script's own arguments; protojs options are not
    // included.
    std::vector<char*> scriptArgv;
    scriptArgv.push_back(argv[0]);
    if (scriptArgStart <= argc && !executeCode && filename != "eval")
        scriptArgv.push_back(argv[scriptArgStart - 1]);
    for (int k = scriptArgStart; k < argc; ++k) scriptArgv.push_back(argv[k]);
    const int scriptArgc = static_cast<int>(scriptArgv.size());
    scriptArgv.push_back(nullptr);

    // Handle version flag
    if (showVersion) {
        std::cout << "protoJS v0.1.0" << std::endl;
        return 0;
    }

    // Syntax check (`-c` / `--check`): parse the input and exit.  This runs
    // before any runtime, thread pool or module initialisation, so the input
    // is never executed.
    if (syntaxCheck) {
        if (executeCode) {
            std::cerr << "protojs: either --check or -e can be used, not both" << std::endl;
            return 9;
        }
        if (code.empty()) {
            std::cerr << "No code to check" << std::endl;
            printUsage(argv[0]);
            return 1;
        }
        return checkSyntaxOnly(code, filename, inputTypeModule);
    }

    // If no file and no -e, start REPL
    if (code.empty() && !executeCode) {
        protojs::JSContextWrapper wrapper(cpuThreads, ioThreads, ioFactor);

        // protoCore is the single execution path (compile → load → run).
        wrapper.setUseProtoEval(true);

        installRuntimeGlobals(wrapper, scriptArgc, scriptArgv.data(), filename, /*minimal=*/false);

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
        installRuntimeGlobals(wrapper, scriptArgc, scriptArgv.data(), filename, /*minimal=*/true);
        JSValue result = wrapper.eval(code, filename, inputTypeModule);
        JS_FreeValue(wrapper.getJSContext(), result);
        return 0;
    }

    // JSON.stringify / JSON.parse polyfill is prepended to the user's
    // code below rather than eval'd separately.  In the current runtime,
    // function references defined in one wrapper.eval call do not work
    // correctly when called from a later one (the function's bytecode is
    // keyed by module-relative bcId, which goes stale once that module's
    // compile-time tables are released).  Prepending keeps the polyfill
    // and user code in the same module so the references stay valid.
    installRuntimeGlobals(wrapper, scriptArgc, scriptArgv.data(), filename, /*minimal=*/false);

    // Evaluate preload files as scripts to set up globals (e.g., harness for test262).
    for (const auto& preload : preloadFiles) {
        std::ifstream pf(preload, std::ios::binary);
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

    // An exception that escaped the main script ends the process, as in Node:
    // eval has already reported it, and work the script queued must not run.
    // exitNow, not return: worker and I/O threads may still be running.
    if (JS_IsException(result)) {
        JS_FreeValue(wrapper.getJSContext(), result);
        protojs::platform::exitNow(1);
    }
    // The end of the first turn: a rejection nobody handled is fatal. Every
    // later turn (each event-loop callback) ends with the same checks.
    protojs::endOfTurnChecks(wrapper.getProtoContext());
    protojs::EventLoop::getInstance().setEndOfTurnHook([&wrapper]() {
        protojs::endOfTurnChecks(wrapper.getProtoContext());
    });

    // Print result if -p flag is set
    if (printResult && !JS_IsException(result) && !JS_IsUndefined(result)) {
        const char* resultStr = JS_ToCString(wrapper.getJSContext(), result);
        if (resultStr) {
            std::cout << resultStr << std::endl;
            JS_FreeCString(wrapper.getJSContext(), resultStr);
        }
    }

    // Process the event loop while work is pending (Deferreds, workers,
    // servers, sockets, I/O), as Node.js does: there is no time limit.  Until
    // 2026-10-03 protojs gave up after 180 seconds, which ended long
    // computations and servers alike.  PROTOJS_EXIT_TIMEOUT_SECONDS restores
    // a limit for diagnostics.
    auto start = std::chrono::steady_clock::now();
    long long timeoutSeconds = 0;
    if (const char* t = std::getenv("PROTOJS_EXIT_TIMEOUT_SECONDS")) timeoutSeconds = std::atoll(t);
    const auto timeout = std::chrono::seconds(timeoutSeconds);

    // PROTOJS_GC_STATS census, printed to stderr at exit.
    //
    // A fixture that waits for something and then passes has to prove it actually
    // waited for what it claims. Under a heap ceiling this census is that proof: if
    // no collection cycle ever ran, then no thread ever had to wait for memory, the
    // blocking-join guards were never exercised, and the case established nothing.
    //
    // Two numbers, both straight from protoCore's public atomics:
    //   cycles         ProtoSpace::getGCCycleCount() -- monotone, so a non-zero
    //                  value is unambiguous evidence that collection ran.
    //   reclaimed-last ProtoSpace::reclaimedLastCycle -- cells the most recently
    //                  COMPLETED cycle swept into the freelist. Non-zero means a
    //                  cycle not only ran but freed memory, which is the condition
    //                  protoCore itself uses as its out-of-memory signal.
    //
    // There is deliberately no running total: protoCore keeps none, and one
    // accumulated from 10 ms samples here would silently miss cycles and read as
    // cumulative while being a lower bound. A number that looks exact and is not is
    // worse than no number.
    proto::ProtoSpace* censusSpace = wrapper.getProtoSpace();
    const bool gcStats = std::getenv("PROTOJS_GC_STATS") != nullptr;

    EventLoopTimerResolution timerResolution;
    while (protojs::EventLoop::getInstance().hasPendingCallbacks() ||
           protojs::EventLoop::getInstance().hasPendingOperations() ||
           protojs::GcOrphanQueue::pending() > 0 ||
           protojs::WorkerThreadsModule::getActiveWorkerCount() > 0 ||
           protojs::Deferred::getActiveDeferredCount() > 0 ||
           protojs::ProtoDeferred::getActiveCount() > 0 ||
           protojs::HTTPModule::getActiveServerCount() > 0 ||
           protojs::HTTPModule::getActiveClientCount() > 0 ||
           protojs::NetModule::getActiveCount() > 0 ||
           protojs::FSModule::getActiveCount() > 0) {
        protojs::EventLoop::getInstance().processCallbacks();
        timerResolution.raise();
        {
            // Wake as soon as another thread hands work over (a Deferred
            // settling, an I/O completion); poll the other conditions at
            // least every 10 ms.
            proto::ProtoContext::UnmanagedScope u(wrapper.getProtoContext());
            protojs::EventLoop::getInstance().waitForCallbacks(std::chrono::milliseconds(10));
        }

        auto now = std::chrono::steady_clock::now();
        if (timeoutSeconds > 0 && now - start > timeout) {
            std::cerr << "Warning: Event loop timeout reached. Some callbacks may not have completed." << std::endl;
            break;
        }
    }

    timerResolution.lower();

    // Process any remaining callbacks one more time
    protojs::EventLoop::getInstance().processCallbacks();
    // And release anything a finalizer handed over after that last pass. Orphans
    // recorded by a finalizer are released by EventLoop::processCallbacks, which is
    // pumped above; this is the belt-and-braces pass for a finalizer that ran during
    // the final callbacks.
    protojs::GcOrphanQueue::drain();

    if (gcStats) {
        std::cerr << "protojs gc: cycles="
                  << (censusSpace ? censusSpace->getGCCycleCount() : 0)
                  << " reclaimed-last=" << (censusSpace
                        ? censusSpace->reclaimedLastCycle.load(std::memory_order_relaxed)
                        : PROTO_UL(0))
                  << " orphans-posted=" << protojs::GcOrphanQueue::postedTotal()
                  << " orphans-released=" << protojs::GcOrphanQueue::releasedTotal()
                  << std::endl;
    }

    const int exitCode = JS_IsException(result) ? 1 : 0;
    JS_FreeValue(wrapper.getJSContext(), result);

    return exitCode;
}
