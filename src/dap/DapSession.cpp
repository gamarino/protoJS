#include "DapSession.h"
#include "DapStdio.h"
#include "DapTransport.h"

#include "../debugger/DebugHooks.h"
#include "../runtime/ProtoBytecodeModule.h"
#include "../runtime/ProtoInterpreter.h"
#include "../runtime/PropertyDictionary.h"
#include "../ArrayElementsStorage.h"
#include "../JSContext.h"
#include "../JSSymbols.h"
#include "../platform/ProcessExit.h"
#include "protoCore.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// The Debug Adapter Protocol server of `protojs --dap`.
//
// Threads:
//   * The program runs on the main thread, exactly as `protojs <program>`
//     runs it (the session calls the command line's runner).  The engine
//     reports every instruction to onInstruction (debugger/DebugHooks.h);
//     a stop parks the main thread there, inside the interpreter, with the
//     stopped frames intact.
//   * A reader thread -- a plain thread, never registered with protoCore and
//     never touching a ProtoObject -- reads the client's requests.  What
//     needs the program's values (stackTrace, scopes, variables, evaluate) it
//     hands to the stopped main thread as a task; the main thread runs it on
//     its own context, where the frames' values are rooted, and answers.
//   * The forwarding threads of DapStdio turn the program's stdout and
//     stderr into `output` events.
//
// The stopped main thread waits in an unmanaged region, so a collection
// another thread starts does not wait for the user; an object a variables
// handle names is pinned in the wrapper's root set until the program resumes.
// ---------------------------------------------------------------------------

namespace protojs::dap {

namespace {

using nlohmann::json;
namespace fs = std::filesystem;
namespace dh = protojs::debughooks;

enum class Mode : int { Free, Entry, StepIn, StepOver, StepOut, Pause };

struct Breakpoint {
    int id = 0;
    int requested = 0;     // the line the client asked for
    int resolved = 0;      // the first line with code at or after it (0: unknown)
    std::string condition;
};

struct BreakpointFile {
    std::string clientPath;   // the path as the client spelled it
    std::vector<Breakpoint> breakpoints;
};

// What the debugger knows of a file name a function was compiled from.
struct FileInfo {
    std::string key;          // normalised path (fileKey), empty if none
    std::string displayPath;  // the path reported to the client
    bool isUserFile = false;  // a file on disk: stops happen only there
};

// A variables handle.
struct VarRef {
    enum Kind { Locals, Closure, Script, Object } kind;
    int frame;                                   // frame index (Locals/Closure/Script)
    const proto::ProtoObject* object = nullptr;  // Object: the object to expand
};

// What an exception stop reports (stopped event, exceptionInfo).
struct ExceptionStop {
    std::string text;         // "TypeError: x is not a function"
    std::string description;  // the text, and where it was raised
    std::string breakMode;    // "always" (All Exceptions), "unhandled" (Uncaught)
    std::string typeName;     // the class (Error subclass), or the type of a non-object
    std::string message;
    std::string stackTrace;
};

// The stop in progress (main thread only).
struct StopState {
    proto::ProtoContext* ctx = nullptr;
    std::vector<dh::Activation>* stack = nullptr;
    size_t depth = 0;                            // activations at the stop
    std::vector<VarRef> refs;                    // handle n is refs[n - 1]
    std::vector<proto::ProtoRootSet::Handle> pins;
};

thread_local bool t_isDebuggee = false;

bool isIdentifier(const std::string& s) {
    if (s.empty()) return false;
    auto start = [](unsigned char c) { return std::isalpha(c) || c == '_' || c == '$' || c >= 0x80; };
    if (!start(static_cast<unsigned char>(s[0]))) return false;
    for (char ch : s) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (!(start(c) || std::isdigit(c))) return false;
    }
    static const std::set<std::string> reserved = {
        "arguments", "await", "break", "case", "catch", "class", "const", "continue",
        "debugger", "default", "delete", "do", "else", "enum", "eval", "export",
        "extends", "false", "finally", "for", "function", "if", "implements",
        "import", "in", "instanceof", "interface", "let", "new", "null", "package",
        "private", "protected", "public", "return", "static", "super", "switch",
        "this", "throw", "true", "try", "typeof", "var", "void", "while", "with",
        "yield"};
    return reserved.count(s) == 0;
}

std::string quoteString(const std::string& s, size_t limit) {
    std::string out = "\"";
    size_t n = 0;
    for (char ch : s) {
        if (++n > limit) { out += "\xE2\x80\xA6"; break; }   // an ellipsis
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\x%02x", static_cast<unsigned>(static_cast<unsigned char>(ch)));
                    out += buf;
                } else {
                    out += ch;
                }
        }
    }
    return out + "\"";
}

std::string formatDouble(double d) {
    if (std::isnan(d)) return "NaN";
    if (std::isinf(d)) return d < 0 ? "-Infinity" : "Infinity";
    if (d == 0 && std::signbit(d)) return "-0";
    char buf[64];
    if (d == std::trunc(d) && std::abs(d) < 1e21) {
        long long iv = static_cast<long long>(d);
        if (static_cast<double>(iv) == d) {
            std::snprintf(buf, sizeof buf, "%lld", iv);
            return buf;
        }
    }
    for (int p = 1; p <= 17; ++p) {
        std::snprintf(buf, sizeof buf, "%.*g", p, d);
        double check = 0.0;
        std::sscanf(buf, "%lf", &check);
        if (check == d) break;
    }
    return buf;
}

std::string toUtf8(proto::ProtoContext* ctx, const proto::ProtoString* s) {
    std::string out;
    if (s) s->toUTF8String(ctx, out);
    return out;
}

class Session {
public:
    Session() : transport_(stdio_.protocolIn(), stdio_.protocolOut()) {
        std::error_code ec;
        baseDir_ = fs::current_path(ec).string();
    }

    int run(char* argv0, ProgramRunner runProgram);

    // Engine hooks (debugger/DebugHooks.h).
    void onInstruction(proto::ProtoContext* ctx, std::vector<dh::Activation>& stack);
    void onModuleLoaded(const ProtoBytecodeModule& root);
    void onThrow(proto::ProtoContext* ctx, std::vector<dh::Activation>& stack,
                 const proto::ProtoObject* value, dh::ThrowKind kind);
    void onCatch(const proto::ProtoObject* value, bool finallyOnly);
    // Every way the process ends goes through here once.
    void endSession(int exitCode);

private:
    // --- protocol --------------------------------------------------------
    void readerLoop();
    void handleRequest(const json& req);
    void handleLaunch(const json& req);
    void handleSetBreakpoints(const json& req);
    void handleResume(const json& req, const std::string& command);
    void handleSetExceptionBreakpoints(const json& req);
    void handleExceptionInfo(const json& req);
    void handleDisconnect(const json& req, bool terminate);
    void postTask(const json& req, std::function<void(StopState&)> task);

    void sendResponse(const json& req, const json& body = json::object());
    void sendError(const json& req, const std::string& message);
    void sendEvent(const std::string& event, const json& body = json::object());
    void sendOutput(const std::string& category, const std::string& text) {
        sendEvent("output", {{"category", category}, {"output", text}});
    }

    // --- breakpoints -----------------------------------------------------
    std::string fileKey(const std::string& path) const;
    const FileInfo& fileInfo(const std::string& debugFile);
    bool breakpointAt(const std::string& key, int line, Breakpoint& out);
    static void resolve(Breakpoint& bp, const std::set<int>& lines);
    json breakpointJson(const Breakpoint& bp, const std::set<int>* lines) const;

    // --- stops (main thread) ---------------------------------------------
    void stop(proto::ProtoContext* ctx, std::vector<dh::Activation>& stack,
              const char* reason, const std::vector<int>& hitIds,
              const ExceptionStop* exception = nullptr);
    ExceptionStop describeException(proto::ProtoContext* ctx, std::vector<dh::Activation>& stack,
                                    const proto::ProtoObject* value, dh::ThrowKind kind,
                                    bool innermostIsUser);
    void setInFlight(const proto::ProtoObject* value);
    bool conditionHolds(proto::ProtoContext* ctx, std::vector<dh::Activation>& stack,
                        const std::string& condition);
    const dh::Activation* frameAt(StopState& st, int frameIndex) const;
    void doStackTrace(StopState& st, const json& req);
    void doScopes(StopState& st, const json& req);
    void doVariables(StopState& st, const json& req);
    void doEvaluate(StopState& st, const json& req);
    // Evaluates `expr` in frame `frameIndex`. True with the value in `out`;
    // false with the error text in `error`.
    bool evaluate(StopState& st, int frameIndex, const std::string& expr,
                  const proto::ProtoObject*& out, std::string& error);
    int handleFor(StopState& st, const proto::ProtoObject* v);
    json variable(StopState& st, const std::string& name, const proto::ProtoObject* v);
    std::string format(proto::ProtoContext* ctx, const proto::ProtoObject* v, int depth = 0);
    bool expandable(proto::ProtoContext* ctx, const proto::ProtoObject* v);
    void children(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                  std::vector<std::pair<std::string, const proto::ProtoObject*>>& out);
    std::string displayName(const dh::Activation& a) const;

    DapStdio stdio_;                  // before transport_: it owns the streams
    DapTransport transport_;
    std::atomic<int> seq_{1};
    std::string baseDir_;

    // Session state, guarded by mu_.
    std::mutex mu_;
    std::condition_variable cv_;
    bool launchReceived_ = false;
    bool configDone_ = false;
    bool inputClosed_ = false;
    bool disconnected_ = false;
    bool programFinished_ = false;
    json launchArgs_;
    bool stopped_ = false;
    std::deque<std::function<void(StopState&)>> tasks_;
    bool resumeReady_ = false;
    Mode resumeMode_ = Mode::Free;
    std::optional<ExceptionStop> exception_;   // the current stop's, if it is one

    // Exception breakpoints (setExceptionBreakpoints).
    std::atomic<bool> breakRaised_{false};
    std::atomic<bool> breakUncaught_{false};
    // The exception last reported, until a `catch` takes it: its rethrows (a
    // `finally` block's, an `await` of the promise it rejected) do not stop
    // again.  Pinned in the wrapper's root set (main thread only).
    const proto::ProtoObject* inFlight_ = nullptr;
    std::optional<proto::ProtoRootSet::Handle> inFlightPin_;

    // Read by the main thread before every new line; written by the reader.
    std::atomic<int> mode_{static_cast<int>(Mode::Free)};
    std::atomic<bool> haveBreakpoints_{false};
    std::atomic<bool> ended_{false};
    int stepDepth_ = 0;               // main thread only
    StopState* stop_ = nullptr;       // main thread only

    // Breakpoints and the lines that have code, by file key; guarded by bpMu_.
    std::mutex bpMu_;
    std::map<std::string, BreakpointFile> breakpoints_;
    std::map<std::string, std::set<int>> codeLines_;
    int nextBreakpointId_ = 1;

    std::unordered_map<std::string, FileInfo> fileInfos_;   // main thread only
};

Session* g_session = nullptr;

void hookInstruction(proto::ProtoContext* ctx, std::vector<dh::Activation>& stack) {
    if (g_session) g_session->onInstruction(ctx, stack);
}
void hookModuleLoaded(const ProtoBytecodeModule& root) {
    if (g_session) g_session->onModuleLoaded(root);
}
void hookThrow(proto::ProtoContext* ctx, std::vector<dh::Activation>& stack,
               const proto::ProtoObject* value, dh::ThrowKind kind) {
    if (g_session) g_session->onThrow(ctx, stack, value, kind);
}
void hookCatch(const proto::ProtoObject* value, bool finallyOnly) {
    if (g_session) g_session->onCatch(value, finallyOnly);
}
void hookExit() {
    if (g_session) g_session->endSession(platform::exitStatus());
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

int Session::run(char* argv0, ProgramRunner runProgram) {
    stdio_.startForwarding([this](const char* category, const std::string& text) {
        sendOutput(category, text);
    });
    // Detached: at the end it may still be blocked reading the client.
    std::thread([this] { readerLoop(); }).detach();

    // Wait for the program to debug and for the client's breakpoints.
    json args;
    {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] { return (launchReceived_ && configDone_) || inputClosed_ || disconnected_; });
        if (!launchReceived_ || inputClosed_ || disconnected_) return 0;
        args = launchArgs_;
    }

    const std::string program = args.value("program", "");
    std::vector<std::string> scriptArgs;
    if (args.contains("args") && args["args"].is_array())
        for (const auto& a : args["args"])
            if (a.is_string()) scriptArgs.push_back(a.get<std::string>());
    if (args.contains("env") && args["env"].is_object()) {
        for (auto it = args["env"].begin(); it != args["env"].end(); ++it) {
            if (!it.value().is_string()) continue;
#if defined(_WIN32)
            _putenv_s(it.key().c_str(), it.value().get<std::string>().c_str());
#else
            setenv(it.key().c_str(), it.value().get<std::string>().c_str(), 1);
#endif
        }
    }
    if (args.value("stopOnEntry", false)) mode_.store(static_cast<int>(Mode::Entry));

    // The program's process.exit, an uncaught exception and every other way
    // out end the session through the exit hook.
    platform::addExitHook(&hookExit);
    dh::enable(&hookInstruction, &hookModuleLoaded);
    dh::setExceptionHandlers(&hookThrow, &hookCatch);
    t_isDebuggee = true;

    std::vector<std::string> argvStore;
    argvStore.push_back(argv0);
    argvStore.push_back(program);
    for (auto& a : scriptArgs) argvStore.push_back(a);
    std::vector<char*> argvPtrs;
    for (auto& s : argvStore) argvPtrs.push_back(s.data());
    argvPtrs.push_back(nullptr);

    const int status = runProgram(static_cast<int>(argvStore.size()), argvPtrs.data());
    endSession(status);

    // The client answers `terminated` with a disconnect.
    {
        std::unique_lock<std::mutex> lk(mu_);
        programFinished_ = true;
        cv_.wait_for(lk, std::chrono::seconds(10), [&] { return inputClosed_ || disconnected_; });
    }
    return status;
}

void Session::endSession(int exitCode) {
    if (ended_.exchange(true)) return;
    // The program's last output goes before `terminated`.
    stdio_.finish(nullptr);
    bool disconnected;
    {
        std::lock_guard<std::mutex> lk(mu_);
        disconnected = disconnected_;
    }
    if (disconnected) return;
    sendEvent("terminated");
    sendEvent("exited", {{"exitCode", exitCode}});
}

// ---------------------------------------------------------------------------
// The reader thread
// ---------------------------------------------------------------------------

void Session::readerLoop() {
    for (;;) {
        std::optional<json> msg = transport_.readMessage();
        if (!msg) break;
        if (msg->value("type", "") != "request") continue;
        handleRequest(*msg);
        std::lock_guard<std::mutex> lk(mu_);
        if (disconnected_) return;
    }
    bool running;
    {
        std::lock_guard<std::mutex> lk(mu_);
        inputClosed_ = true;
        running = launchReceived_ && configDone_ && !programFinished_;
    }
    cv_.notify_all();
    // The client is gone: a program it was debugging does not outlive it.
    if (running) {
        ended_.store(true);
        platform::exitNow(0);
    }
}

void Session::handleRequest(const json& req) {
    const std::string command = req.value("command", "");
    if (command == "initialize") {
        sendResponse(req, {
            {"supportsConfigurationDoneRequest", true},
            {"supportsConditionalBreakpoints", true},
            {"supportsEvaluateForHovers", true},
            {"supportsTerminateRequest", true},
            {"supportsExceptionInfoRequest", true},
            {"exceptionBreakpointFilters", json::array({
                {{"filter", "raised"}, {"label", "All Exceptions"}, {"default", false},
                 {"description", "Stop at every exception thrown in your code, caught or not, "
                                 "before it unwinds the stack."}},
                {{"filter", "uncaught"}, {"label", "Uncaught Exceptions"}, {"default", false},
                 {"description", "Stop where an exception is thrown that no try/catch on the "
                                 "stack catches.  A throw in an async function or a promise "
                                 "callback rejects a promise instead: it is not uncaught."}},
            })},
        });
        sendEvent("initialized");
    } else if (command == "launch") {
        handleLaunch(req);
    } else if (command == "setBreakpoints") {
        handleSetBreakpoints(req);
    } else if (command == "setExceptionBreakpoints") {
        handleSetExceptionBreakpoints(req);
    } else if (command == "exceptionInfo") {
        handleExceptionInfo(req);
    } else if (command == "configurationDone") {
        sendResponse(req);
        {
            std::lock_guard<std::mutex> lk(mu_);
            configDone_ = true;
        }
        cv_.notify_all();
    } else if (command == "threads") {
        sendResponse(req, {{"threads", json::array({{{"id", 1}, {"name", "main"}}})}});
    } else if (command == "continue" || command == "next" || command == "stepIn" ||
               command == "stepOut") {
        handleResume(req, command);
    } else if (command == "pause") {
        mode_.store(static_cast<int>(Mode::Pause));
        sendResponse(req);
    } else if (command == "stackTrace") {
        postTask(req, [this, req](StopState& st) { doStackTrace(st, req); });
    } else if (command == "scopes") {
        postTask(req, [this, req](StopState& st) { doScopes(st, req); });
    } else if (command == "variables") {
        postTask(req, [this, req](StopState& st) { doVariables(st, req); });
    } else if (command == "evaluate") {
        postTask(req, [this, req](StopState& st) { doEvaluate(st, req); });
    } else if (command == "disconnect") {
        handleDisconnect(req, false);
    } else if (command == "terminate") {
        handleDisconnect(req, true);
    } else {
        sendResponse(req);
    }
}

void Session::handleLaunch(const json& req) {
    json args = req.value("arguments", json::object());
    const std::string program = args.value("program", "");
    std::error_code ec;
    if (program.empty() || !fs::is_regular_file(fs::path(program), ec)) {
        sendError(req, program.empty() ? "launch: 'program' is missing"
                                       : "launch: cannot open " + program);
        sendEvent("terminated");
        return;
    }
    const std::string cwd = args.value("cwd", "");
    if (!cwd.empty()) {
        fs::current_path(fs::path(cwd), ec);
        if (ec) {
            sendError(req, "launch: cannot change to the directory " + cwd);
            sendEvent("terminated");
            return;
        }
    }
    {
        std::error_code ec2;
        baseDir_ = fs::current_path(ec2).string();
    }
    {
        // The program's own path, as the client spells it, names its file.
        std::lock_guard<std::mutex> lk(bpMu_);
        BreakpointFile& f = breakpoints_[fileKey(program)];
        if (f.clientPath.empty()) f.clientPath = program;
    }
    sendResponse(req);
    {
        std::lock_guard<std::mutex> lk(mu_);
        launchArgs_ = args;
        launchReceived_ = true;
    }
    cv_.notify_all();
}

void Session::handleSetBreakpoints(const json& req) {
    const json args = req.value("arguments", json::object());
    const std::string path = args.value("source", json::object()).value("path", "");
    const std::string key = fileKey(path);
    json result = json::array();
    {
        std::lock_guard<std::mutex> lk(bpMu_);
        BreakpointFile& f = breakpoints_[key];
        f.clientPath = path;
        f.breakpoints.clear();   // setBreakpoints replaces the file's list
        auto lines = codeLines_.find(key);
        const std::set<int>* known = lines == codeLines_.end() ? nullptr : &lines->second;
        if (args.contains("breakpoints") && args["breakpoints"].is_array()) {
            for (const auto& b : args["breakpoints"]) {
                Breakpoint bp;
                bp.id = nextBreakpointId_++;
                bp.requested = b.value("line", 0);
                bp.condition = b.value("condition", "");
                if (known) resolve(bp, *known);
                f.breakpoints.push_back(bp);
                result.push_back(breakpointJson(bp, known));
            }
        }
        bool any = false;
        for (const auto& [k, file] : breakpoints_) any = any || !file.breakpoints.empty();
        haveBreakpoints_.store(any);
    }
    sendResponse(req, {{"breakpoints", result}});
}

void Session::handleResume(const json& req, const std::string& command) {
    Mode m = Mode::Free;
    if (command == "next") m = Mode::StepOver;
    else if (command == "stepIn") m = Mode::StepIn;
    else if (command == "stepOut") m = Mode::StepOut;
    bool wasStopped;
    {
        std::lock_guard<std::mutex> lk(mu_);
        wasStopped = stopped_;
        if (wasStopped) {
            resumeMode_ = m;
            resumeReady_ = true;
        }
    }
    cv_.notify_all();
    if (command == "continue") sendResponse(req, {{"allThreadsContinued", true}});
    else sendResponse(req);
}

void Session::handleSetExceptionBreakpoints(const json& req) {
    const json args = req.value("arguments", json::object());
    std::vector<std::string> filters;
    if (args.contains("filters") && args["filters"].is_array())
        for (const auto& f : args["filters"])
            if (f.is_string()) filters.push_back(f.get<std::string>());
    if (args.contains("filterOptions") && args["filterOptions"].is_array())
        for (const auto& f : args["filterOptions"])
            if (f.is_object()) filters.push_back(f.value("filterId", ""));
    bool raised = false, uncaught = false;
    json result = json::array();
    for (const auto& f : filters) {
        const bool known = f == "raised" || f == "uncaught";
        raised = raised || f == "raised";
        uncaught = uncaught || f == "uncaught";
        json b = {{"verified", known}};
        if (!known) b["message"] = "unknown exception filter " + f;
        result.push_back(std::move(b));
    }
    breakRaised_.store(raised);
    breakUncaught_.store(uncaught);
    sendResponse(req, {{"breakpoints", result}});
}

void Session::handleExceptionInfo(const json& req) {
    std::optional<ExceptionStop> e;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stopped_) e = exception_;
    }
    if (!e) {
        sendError(req, "the program is not stopped at an exception");
        return;
    }
    sendResponse(req, {
        {"exceptionId", e->typeName},
        {"description", e->description},
        {"breakMode", e->breakMode},
        {"details", {{"typeName", e->typeName}, {"message", e->message},
                     {"stackTrace", e->stackTrace}}},
    });
}

void Session::handleDisconnect(const json& req, bool terminate) {
    sendResponse(req);
    bool finished;
    {
        std::lock_guard<std::mutex> lk(mu_);
        finished = programFinished_ || !(launchReceived_ && configDone_);
        if (!terminate) disconnected_ = true;
    }
    cv_.notify_all();
    if (finished) return;
    // The program is still running (or stopped): end it.  For `terminate`
    // the exit hook still reports `terminated` and `exited`.
    if (!terminate) ended_.store(true);
    platform::exitNow(0);
}

void Session::postTask(const json& req, std::function<void(StopState&)> task) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (stopped_) {
            tasks_.push_back(std::move(task));
            cv_.notify_all();
            return;
        }
    }
    const std::string command = req.value("command", "");
    if (command == "stackTrace") sendResponse(req, {{"stackFrames", json::array()}, {"totalFrames", 0}});
    else if (command == "scopes") sendResponse(req, {{"scopes", json::array()}});
    else if (command == "variables") sendResponse(req, {{"variables", json::array()}});
    else sendError(req, "the program is running: pause it to evaluate");
}

void Session::sendResponse(const json& req, const json& body) {
    transport_.writeMessage({
        {"seq", seq_.fetch_add(1)},
        {"type", "response"},
        {"request_seq", req.value("seq", 0)},
        {"success", true},
        {"command", req.value("command", "")},
        {"body", body},
    });
}

void Session::sendError(const json& req, const std::string& message) {
    transport_.writeMessage({
        {"seq", seq_.fetch_add(1)},
        {"type", "response"},
        {"request_seq", req.value("seq", 0)},
        {"success", false},
        {"command", req.value("command", "")},
        {"message", message},
        {"body", {{"error", {{"id", 1}, {"format", message}, {"showUser", false}}}}},
    });
}

void Session::sendEvent(const std::string& event, const json& body) {
    transport_.writeMessage({
        {"seq", seq_.fetch_add(1)},
        {"type", "event"},
        {"event", event},
        {"body", body},
    });
}

// ---------------------------------------------------------------------------
// Files and breakpoints
// ---------------------------------------------------------------------------

// A path as one key, whoever spells it: absolute, normalised, '/' separated,
// and on Windows lower case (VS Code sends "c:\Users\...", the program may
// name "C:/Users/...").
std::string Session::fileKey(const std::string& path) const {
    if (path.empty() || path[0] == '<') return {};
    fs::path p(path);
    if (p.is_relative()) p = fs::path(baseDir_) / p;
    std::string s = p.lexically_normal().generic_string();
#if defined(_WIN32)
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
#endif
    return s;
}

const FileInfo& Session::fileInfo(const std::string& debugFile) {
    auto it = fileInfos_.find(debugFile);
    if (it != fileInfos_.end()) return it->second;
    FileInfo info;
    info.key = fileKey(debugFile);
    if (!info.key.empty()) {
        std::error_code ec;
        fs::path p(debugFile);
        if (p.is_relative()) p = fs::path(baseDir_) / p;
        p = p.lexically_normal();
        info.isUserFile = fs::is_regular_file(p, ec);
        info.displayPath = p.make_preferred().string();
        std::lock_guard<std::mutex> lk(bpMu_);
        auto f = breakpoints_.find(info.key);
        if (f != breakpoints_.end() && !f->second.clientPath.empty())
            info.displayPath = f->second.clientPath;
    }
    return fileInfos_.emplace(debugFile, std::move(info)).first->second;
}

void Session::resolve(Breakpoint& bp, const std::set<int>& lines) {
    auto it = lines.lower_bound(bp.requested);
    bp.resolved = it == lines.end() ? 0 : *it;
}

json Session::breakpointJson(const Breakpoint& bp, const std::set<int>* lines) const {
    json j = {{"id", bp.id}, {"line", bp.resolved ? bp.resolved : bp.requested}};
    if (bp.resolved) {
        j["verified"] = true;
    } else {
        j["verified"] = false;
        j["message"] = lines ? "no code at or after this line"
                             : "the file has not been loaded yet";
    }
    return j;
}

bool Session::breakpointAt(const std::string& key, int line, Breakpoint& out) {
    std::lock_guard<std::mutex> lk(bpMu_);
    auto f = breakpoints_.find(key);
    if (f == breakpoints_.end()) return false;
    for (const Breakpoint& bp : f->second.breakpoints) {
        if ((bp.resolved ? bp.resolved : bp.requested) == line) {
            out = bp;
            return true;
        }
    }
    return false;
}

void Session::onModuleLoaded(const ProtoBytecodeModule& root) {
    // The lines of each file that have code, from every function loaded.
    std::map<std::string, std::set<int>> found;
    std::string lastFile, lastKey;
    auto add = [&](const ProtoBytecodeModule& m) {
        if (m.debugFile.empty()) return;
        if (m.debugFile != lastFile) { lastFile = m.debugFile; lastKey = fileKey(lastFile); }
        if (lastKey.empty()) return;
        std::set<int>& s = found[lastKey];
        // A line has code when its entry covers an instruction, outside the
        // prologue (entry 0, the function's own line, when there are more).
        const size_t n = m.debugLines.size();
        for (size_t i = n > 1 ? 1 : 0; i < n; i++) {
            if (i + 1 < n && m.debugLinePcs[i + 1] == m.debugLinePcs[i]) continue;
            if (m.debugLines[i] > 0) s.insert(m.debugLines[i]);
        }
    };
    add(root);
    for (const auto& nf : root.nestedFunctions) add(nf);

    std::vector<json> changed;
    {
        std::lock_guard<std::mutex> lk(bpMu_);
        for (auto& [key, lines] : found) {
            std::set<int>& all = codeLines_[key];
            all.insert(lines.begin(), lines.end());
            auto f = breakpoints_.find(key);
            if (f == breakpoints_.end()) continue;
            for (Breakpoint& bp : f->second.breakpoints) {
                const int before = bp.resolved;
                resolve(bp, all);
                if (bp.resolved != before) changed.push_back(breakpointJson(bp, &all));
            }
        }
    }
    for (auto& bp : changed) sendEvent("breakpoint", {{"reason", "changed"}, {"breakpoint", bp}});
}

// ---------------------------------------------------------------------------
// Stops (main thread)
// ---------------------------------------------------------------------------

void Session::onInstruction(proto::ProtoContext* ctx, std::vector<dh::Activation>& stack) {
    if (!t_isDebuggee) return;
    const Mode mode = static_cast<Mode>(mode_.load(std::memory_order_relaxed));
    const bool haveBreakpoints = haveBreakpoints_.load(std::memory_order_relaxed);
    // Running free with no breakpoint: nothing to look at (a stop refreshes
    // the lines the frames are on, see stop()).
    if (mode == Mode::Free && !haveBreakpoints) return;
    dh::Activation& a = stack.back();
    bool prologue = false;
    const int line = dh::lineForPc(a.module, a.pc, &prologue);
    if (line <= 0 || prologue) return;
    // A new line: another line, or the same one again after a jump back (the
    // next iteration of a loop written on one line).
    const bool newLine = line != a.lastLine || a.pc < a.lastPc;
    a.lastLine = line;
    a.lastPc = a.pc;
    if (!newLine) return;

    const FileInfo& file = fileInfo(a.module->debugFile);
    if (!file.isUserFile) return;   // never stop inside code that is not a file

    const int depth = static_cast<int>(stack.size());
    const char* reason = nullptr;
    switch (mode) {
        case Mode::Entry:    reason = "entry"; break;
        case Mode::StepIn:   reason = "step"; break;
        case Mode::StepOver: if (depth <= stepDepth_) reason = "step"; break;
        case Mode::StepOut:  if (depth < stepDepth_) reason = "step"; break;
        case Mode::Pause:    reason = "pause"; break;
        case Mode::Free:     break;
    }
    std::vector<int> hit;
    if (haveBreakpoints) {
        Breakpoint bp;
        if (breakpointAt(file.key, line, bp) &&
            (bp.condition.empty() || conditionHolds(ctx, stack, bp.condition))) {
            hit.push_back(bp.id);
            if (!reason) reason = "breakpoint";
        }
    }
    if (reason) stop(ctx, stack, reason, hit);
}

void Session::stop(proto::ProtoContext* ctx, std::vector<dh::Activation>& stack,
                   const char* reason, const std::vector<int>& hitIds,
                   const ExceptionStop* exception) {
    mode_.store(static_cast<int>(Mode::Free));
    // A line printed without a newline is still in a buffer.
    std::cout.flush();
    std::fflush(stdout);
    std::fflush(stderr);

    // Every frame is on the line it stopped at: stepping compares with it.
    for (dh::Activation& f : stack) {
        f.lastLine = dh::lineForPc(f.module, f.pc);
        f.lastPc = f.pc;
    }

    StopState st;
    st.ctx = ctx;
    st.stack = &stack;
    st.depth = stack.size();
    stop_ = &st;
    {
        std::lock_guard<std::mutex> lk(mu_);
        stopped_ = true;
        resumeReady_ = false;
        tasks_.clear();
        if (exception) exception_ = *exception;
        else exception_.reset();
    }
    json body = {{"reason", reason}, {"threadId", 1}, {"allThreadsStopped", true}};
    if (!hitIds.empty()) body["hitBreakpointIds"] = hitIds;
    if (exception) {
        body["description"] = exception->description;
        body["text"] = exception->text;
    }
    sendEvent("stopped", body);

    Mode next = Mode::Free;
    for (;;) {
        std::function<void(StopState&)> task;
        {
            // Waiting for the user: a collection must not wait for us.
            proto::ProtoContext::UnmanagedScope unmanaged(ctx);
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return !tasks_.empty() || resumeReady_; });
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop_front();
            } else {
                stopped_ = false;
                exception_.reset();
                next = resumeMode_;
            }
        }
        if (!task) break;
        // A task may run JavaScript (evaluate): it must not stop again.
        dh::setSuppressed(true);
        try {
            task(st);
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[dap] %s\n", e.what());
        } catch (...) {
        }
        dh::setSuppressed(false);
    }

    if (JSContextWrapper* w = JSContextWrapper::current()) {
        if (proto::ProtoRootSet* rs = w->getRootSet())
            for (auto h : st.pins) rs->remove(h);
    }
    stop_ = nullptr;
    // `stack` may have been reallocated by the code a task ran: only its size
    // is read from here on.
    stepDepth_ = static_cast<int>(stack.size());
    mode_.store(static_cast<int>(next));
}

// ---------------------------------------------------------------------------
// Exception breakpoints (main thread)
// ---------------------------------------------------------------------------

void Session::onThrow(proto::ProtoContext* ctx, std::vector<dh::Activation>& stack,
                      const proto::ProtoObject* value, dh::ThrowKind kind) {
    if (!t_isDebuggee) return;
    const bool raised = breakRaised_.load(std::memory_order_relaxed);
    const bool uncaught = breakUncaught_.load(std::memory_order_relaxed);
    if (!raised && !uncaught) return;
    if (!value) value = PROTO_NONE;
    // A rethrow of the exception already reported: a `finally` block's, the
    // `await` of the promise it rejected.
    if (inFlight_ && value == inFlight_) return;
    // The innermost frame of the user's own code (a file on disk).
    size_t user = stack.size();
    for (size_t i = stack.size(); i-- > 0;) {
        const dh::Activation& a = stack[i];
        if (a.module && fileInfo(a.module->debugFile).isUserFile) { user = i; break; }
    }
    if (user == stack.size()) return;
    const bool innermostIsUser = user + 1 == stack.size();
    const char* breakMode = nullptr;
    if (uncaught && !dh::throwWillBeCaught(stack)) breakMode = "unhandled";
    else if (raised && innermostIsUser) breakMode = "always";
    if (!breakMode) return;

    setInFlight(value);   // which also keeps it alive during the stop
    ExceptionStop e = describeException(ctx, stack, value, kind, innermostIsUser);
    e.breakMode = breakMode;
    stop(ctx, stack, "exception", {}, &e);
}

void Session::onCatch(const proto::ProtoObject* value, bool finallyOnly) {
    if (!t_isDebuggee || finallyOnly || !inFlight_ || value != inFlight_) return;
    setInFlight(nullptr);
}

void Session::setInFlight(const proto::ProtoObject* value) {
    JSContextWrapper* w = JSContextWrapper::current();
    proto::ProtoRootSet* rs = w ? w->getRootSet() : nullptr;
    if (inFlightPin_ && rs) rs->remove(*inFlightPin_);
    inFlightPin_.reset();
    inFlight_ = value;
    if (value && rs) inFlightPin_ = rs->add(value);
}

bool Session::conditionHolds(proto::ProtoContext* ctx, std::vector<dh::Activation>& stack,
                             const std::string& condition) {
    StopState st;
    st.ctx = ctx;
    st.stack = &stack;
    st.depth = stack.size();
    const proto::ProtoObject* v = nullptr;
    std::string error;
    dh::setSuppressed(true);
    const bool ok = evaluate(st, 0, condition, v, error);
    dh::setSuppressed(false);
    if (!ok) {
        sendOutput("stderr", "breakpoint condition '" + condition + "': " + error + "\n");
        return true;   // a condition that fails stops, as in other debuggers
    }
    if (!v || v == PROTO_NONE || v == PROTO_FALSE || v == getNullSentinel() ||
        v == getUndefinedSentinel())
        return false;
    if (v->isInteger(ctx)) return v->asLong(ctx) != 0;
    if (v->isDouble(ctx)) { double d = v->asDouble(ctx); return d != 0 && !std::isnan(d); }
    if (v->isString(ctx)) return v->asString(ctx)->getSize(ctx) != 0;
    return true;
}

const dh::Activation* Session::frameAt(StopState& st, int frameIndex) const {
    if (frameIndex < 0 || static_cast<size_t>(frameIndex) >= st.depth) return nullptr;
    return &(*st.stack)[st.depth - 1 - static_cast<size_t>(frameIndex)];
}

std::string Session::displayName(const dh::Activation& a) const {
    if (!a.module) return "<unknown>";
    if (!a.module->ownerRoot) return "<toplevel>";
    return a.module->funcName.empty() ? "<anonymous>" : a.module->funcName;
}

void Session::doStackTrace(StopState& st, const json& req) {
    const json args = req.value("arguments", json::object());
    const int start = args.value("startFrame", 0);
    const int levels = args.value("levels", 0);
    json frames = json::array();
    const int total = static_cast<int>(st.depth);
    for (int i = start; i < total; i++) {
        if (levels > 0 && static_cast<int>(frames.size()) >= levels) break;
        const dh::Activation& a = *frameAt(st, i);
        json f = {
            {"id", i + 1},
            {"name", displayName(a)},
            {"line", dh::lineForPc(a.module, a.pc)},
            {"column", 1},
        };
        const FileInfo& file = fileInfo(a.module ? a.module->debugFile : std::string());
        if (file.isUserFile) {
            f["source"] = {{"name", fs::path(file.displayPath).filename().string()},
                           {"path", file.displayPath}};
        } else {
            f["presentationHint"] = "subtle";
        }
        frames.push_back(std::move(f));
    }
    sendResponse(req, {{"stackFrames", frames}, {"totalFrames", total}});
}

void Session::doScopes(StopState& st, const json& req) {
    const int frame = req.value("arguments", json::object()).value("frameId", 1) - 1;
    json scopes = json::array();
    const dh::Activation* a = frameAt(st, frame);
    if (a) {
        std::vector<dh::FrameVar> vars;
        dh::collectFrameVars(*a, vars);
        bool closures = false;
        for (auto& v : vars) closures = closures || v.kind == dh::FrameVar::Closure;
        st.refs.push_back(VarRef{VarRef::Locals, frame});
        scopes.push_back({{"name", "Locals"}, {"presentationHint", "locals"},
                          {"variablesReference", static_cast<int>(st.refs.size())},
                          {"expensive", false}});
        if (closures) {
            st.refs.push_back(VarRef{VarRef::Closure, frame});
            scopes.push_back({{"name", "Closure"},
                              {"variablesReference", static_cast<int>(st.refs.size())},
                              {"expensive", false}});
        }
        // The script's top-level bindings live on a child of the global
        // object (runBytecode's module-scope split).
        const proto::ProtoObject* root = a->pGlobalRoot ? *a->pGlobalRoot : nullptr;
        if (root && root != PROTO_NONE && globalObjectForRoot(st.ctx, root) != root) {
            st.refs.push_back(VarRef{VarRef::Script, frame});
            scopes.push_back({{"name", "Script"},
                              {"variablesReference", static_cast<int>(st.refs.size())},
                              {"expensive", false}});
        }
    }
    sendResponse(req, {{"scopes", scopes}});
}

void Session::doVariables(StopState& st, const json& req) {
    const int ref = req.value("arguments", json::object()).value("variablesReference", 0);
    json vars = json::array();
    if (ref >= 1 && static_cast<size_t>(ref) <= st.refs.size()) {
        const VarRef r = st.refs[static_cast<size_t>(ref) - 1];
        const dh::Activation* a = frameAt(st, r.frame);
        if ((r.kind == VarRef::Locals || r.kind == VarRef::Closure) && a) {
            std::vector<dh::FrameVar> fv;
            dh::collectFrameVars(*a, fv);
            for (auto& v : fv) {
                const bool closure = v.kind == dh::FrameVar::Closure;
                if (closure != (r.kind == VarRef::Closure)) continue;
                vars.push_back(variable(st, v.name, v.value));
            }
        } else if (r.kind == VarRef::Script && a && a->pGlobalRoot) {
            std::vector<std::pair<std::string, const proto::ProtoObject*>> kids;
            children(st.ctx, *a->pGlobalRoot, kids);
            for (auto& [name, v] : kids) vars.push_back(variable(st, name, v));
        } else if (r.kind == VarRef::Object && r.object) {
            std::vector<std::pair<std::string, const proto::ProtoObject*>> kids;
            children(st.ctx, r.object, kids);
            for (auto& [name, v] : kids) vars.push_back(variable(st, name, v));
        }
    }
    sendResponse(req, {{"variables", vars}});
}

void Session::doEvaluate(StopState& st, const json& req) {
    const json args = req.value("arguments", json::object());
    const std::string expr = args.value("expression", "");
    const int frame = args.contains("frameId") ? args.value("frameId", 1) - 1 : 0;
    const proto::ProtoObject* v = nullptr;
    std::string error;
    if (!evaluate(st, frame, expr, v, error)) {
        sendError(req, error);
        return;
    }
    const int handle = expandable(st.ctx, v) ? handleFor(st, v) : 0;
    sendResponse(req, {{"result", format(st.ctx, v)}, {"variablesReference", handle}});
}

bool Session::evaluate(StopState& st, int frameIndex, const std::string& exprIn,
                       const proto::ProtoObject*& out, std::string& error) {
    std::string expr = exprIn;
    while (!expr.empty() && std::isspace(static_cast<unsigned char>(expr.back()))) expr.pop_back();
    while (!expr.empty() && std::isspace(static_cast<unsigned char>(expr.front()))) expr.erase(expr.begin());
    if (expr.empty()) { error = "empty expression"; return false; }
    const dh::Activation* a = frameAt(st, frameIndex);
    if (!a) a = frameAt(st, 0);
    if (!a) { error = "no frame"; return false; }
    proto::ProtoContext* ctx = st.ctx;

    std::vector<dh::FrameVar> fv;
    dh::collectFrameVars(*a, fv);

    // A variable of the frame, read in place.
    for (auto& v : fv) {
        if (v.name == expr) {
            if (!v.value) { error = "ReferenceError: " + expr + " is not initialized"; return false; }
            out = v.value;
            return true;
        }
    }

    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) { error = "no runtime"; return false; }

    // Anything else: a function whose parameters are the frame's variables,
    // called with their values (an assignment to one does not reach the
    // frame).  Globals and the script's bindings resolve as in the program.
    std::vector<std::string> names;
    std::vector<const proto::ProtoObject*> values;
    std::set<std::string> seen;
    for (auto& v : fv) {
        if (!v.value || !isIdentifier(v.name) || !seen.insert(v.name).second) continue;
        names.push_back(v.name);
        values.push_back(v.value);
    }
    std::string params;
    for (size_t i = 0; i < names.size(); i++) params += (i ? "," : "") + names[i];

    const proto::ProtoObject* fn = nullptr;
    std::string compileError;
    for (int attempt = 0; attempt < 2 && !fn; attempt++) {
        const std::string code = attempt == 0
            ? "(function(" + params + ") { return (" + expr + "\n); })"
            : "(function(" + params + ") {\n" + expr + "\n})";
        try {
            fn = wrapper->evalIsolatedToProto(code, "<debugger>");
            if (fn == PROTO_NONE) fn = nullptr;
        } catch (const proto::ProtoException& e) {
            if (attempt == 0) compileError = format(ctx, e.value());
        }
    }
    if (!fn) { error = compileError.empty() ? "cannot compile the expression" : compileError; return false; }

    const proto::ProtoList* argList = ctx->newList();
    for (auto* v : values) argList = argList->appendLast(ctx, v);
    try {
        out = callJSFunction(ctx, fn, a->thisObj ? a->thisObj : PROTO_NONE, argList);
        if (!out) out = PROTO_NONE;
        return true;
    } catch (const proto::ProtoException& e) {
        error = "Uncaught " + format(ctx, e.value());
        return false;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

int Session::handleFor(StopState& st, const proto::ProtoObject* v) {
    if (JSContextWrapper* w = JSContextWrapper::current())
        if (proto::ProtoRootSet* rs = w->getRootSet()) st.pins.push_back(rs->add(v));
    st.refs.push_back(VarRef{VarRef::Object, -1, v});
    return static_cast<int>(st.refs.size());
}

json Session::variable(StopState& st, const std::string& name, const proto::ProtoObject* v) {
    const int handle = (v && expandable(st.ctx, v)) ? handleFor(st, v) : 0;
    return {{"name", name}, {"value", v ? format(st.ctx, v) : "<uninitialized>"},
            {"variablesReference", handle}};
}

// ---------------------------------------------------------------------------
// Values.  Read without running any JavaScript: no getter, no toString.
// ---------------------------------------------------------------------------

namespace {

bool isArrayObject(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    const proto::ProtoString* k = JSSymbols::isArray(ctx);
    return k && v->getAttribute(ctx, k, false) == PROTO_TRUE;
}

bool isClosure(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    const proto::ProtoString* k = JSSymbols::bytecodeId(ctx);
    if (!k) return false;
    const proto::ProtoObject* id = v->getAttribute(ctx, k, false);
    return id && id != PROTO_NONE;
}

std::string stringAttr(proto::ProtoContext* ctx, const proto::ProtoObject* v,
                       const proto::ProtoString* key, bool chain) {
    if (!key) return {};
    const proto::ProtoObject* a = v->getAttribute(ctx, key, chain);
    if (!a || a == PROTO_NONE || !a->isString(ctx)) return {};
    return toUtf8(ctx, a->asString(ctx));
}

bool isPrimitive(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return !v || v == PROTO_NONE || v == getNullSentinel() || v == getUndefinedSentinel() ||
           v->isString(ctx) || v->isBoolean(ctx) || v->isInteger(ctx) || v->isDouble(ctx);
}

struct AttrWalk {
    proto::ProtoContext* ctx;
    std::vector<std::pair<std::string, const proto::ProtoObject*>>* out;
};

void collectAttr(proto::ProtoContext* ctx, void* self, const proto::ProtoString* name,
                 const proto::ProtoObject* value) {
    auto* w = static_cast<AttrWalk*>(self);
    std::string key = toUtf8(ctx, name);
    // protoJS's own bookkeeping attributes (__elements__, __bytecode_id__...).
    if (key.empty() || (key.size() >= 2 && key[0] == '_' && key[1] == '_')) return;
    w->out->emplace_back(std::move(key), value);
}

} // namespace

bool Session::expandable(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (isPrimitive(ctx, v) || v->isMethod(ctx)) return false;
    return true;
}

void Session::children(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                       std::vector<std::pair<std::string, const proto::ProtoObject*>>& out) {
    out.clear();
    if (!obj || isPrimitive(ctx, obj)) return;
    if (isArrayObject(ctx, obj)) {
        if (const proto::ProtoList* els = getArrayElements(ctx, obj)) {
            const size_t n = std::min<size_t>(els->getSize(ctx), 1000);
            for (size_t i = 0; i < n; i++)
                out.emplace_back(std::to_string(i), els->getAt(ctx, static_cast<int>(i)));
        }
    }
    std::vector<std::pair<std::string, const proto::ProtoObject*>> attrs;
    AttrWalk w{ctx, &attrs};
    obj->processOwnAttributes(ctx, &w, &collectAttr);
    std::sort(attrs.begin(), attrs.end(),
              [](const auto& x, const auto& y) { return x.first < y.first; });
    for (auto& p : attrs) out.push_back(p);
    std::vector<std::pair<const proto::ProtoString*, const proto::ProtoObject*>> de;
    dict::entries(ctx, obj, de);
    for (const auto& e : de) out.emplace_back(toUtf8(ctx, e.first), e.second);
}

std::string Session::format(proto::ProtoContext* ctx, const proto::ProtoObject* v, int depth) {
    if (!v) return "<uninitialized>";
    if (v == PROTO_NONE || v == getUndefinedSentinel()) return "undefined";
    if (v == getNullSentinel()) return "null";
    if (v->isString(ctx)) return quoteString(toUtf8(ctx, v->asString(ctx)), depth ? 40 : 1000);
    if (v->isBoolean(ctx)) return v->asBoolean(ctx) ? "true" : "false";
    if (v->isInteger(ctx)) return std::to_string(v->asLong(ctx));
    if (v->isDouble(ctx)) return formatDouble(v->asDouble(ctx));
    if (v->isMethod(ctx)) return "\xC6\x92 native()";   // "f" with hook: a function
    if (isClosure(ctx, v)) {
        const std::string n = stringAttr(ctx, v, JSSymbols::name(ctx), false);
        return "\xC6\x92 " + (n.empty() ? std::string("anonymous") : n) + "()";
    }
    if (isArrayObject(ctx, v)) {
        const proto::ProtoList* els = getArrayElements(ctx, v);
        const size_t n = els ? els->getSize(ctx) : 0;
        std::string s = "Array(" + std::to_string(n) + ")";
        if (depth > 0) return s;
        s += " [";
        for (size_t i = 0; i < n && i < 10; i++) {
            if (i) s += ", ";
            s += format(ctx, els->getAt(ctx, static_cast<int>(i)), depth + 1);
        }
        if (n > 10) s += ", \xE2\x80\xA6";
        return s + "]";
    }
    // An error: "Name: message".
    const std::string message = stringAttr(ctx, v, JSSymbols::message(ctx), false);
    if (!message.empty() || v->getAttribute(ctx, JSSymbols::message(ctx), false) != PROTO_NONE) {
        const std::string name = stringAttr(ctx, v, JSSymbols::name(ctx), true);
        if (!name.empty()) return message.empty() ? name : name + ": " + message;
    }
    // The class: constructor.name along the prototype chain.
    std::string cls;
    if (const proto::ProtoString* ck = JSSymbols::constructor(ctx)) {
        const proto::ProtoObject* c = v->getAttribute(ctx, ck, true);
        if (c && c != PROTO_NONE && !isPrimitive(ctx, c))
            cls = stringAttr(ctx, c, JSSymbols::name(ctx), false);
    }
    const std::string prefix = (cls.empty() || cls == "Object") ? "" : cls + " ";
    if (depth > 0) return prefix + "{\xE2\x80\xA6}";
    std::vector<std::pair<std::string, const proto::ProtoObject*>> kids;
    children(ctx, v, kids);
    std::string s = prefix + "{";
    for (size_t i = 0; i < kids.size() && i < 5; i++) {
        s += (i ? ", " : "") + kids[i].first + ": " + format(ctx, kids[i].second, depth + 1);
    }
    if (kids.size() > 5) s += ", \xE2\x80\xA6";
    return s + "}";
}

ExceptionStop Session::describeException(proto::ProtoContext* ctx, std::vector<dh::Activation>& stack,
                                         const proto::ProtoObject* v, dh::ThrowKind kind,
                                         bool innermostIsUser) {
    ExceptionStop e;
    const bool isObject = !isPrimitive(ctx, v) && !v->isMethod(ctx);
    const proto::ProtoString* messageKey = JSSymbols::message(ctx);
    const proto::ProtoObject* messageAttr =
        (isObject && messageKey) ? v->getAttribute(ctx, messageKey, true) : PROTO_NONE;
    if (messageAttr && messageAttr != PROTO_NONE) {
        // An error: its name (along the prototype chain) and message.
        e.typeName = stringAttr(ctx, v, JSSymbols::name(ctx), true);
        if (e.typeName.empty()) e.typeName = "Error";
        e.message = stringAttr(ctx, v, messageKey, true);
        e.text = e.message.empty() ? e.typeName : e.typeName + ": " + e.message;
    } else {
        if (v == PROTO_NONE || v == getUndefinedSentinel()) e.typeName = "undefined";
        else if (v == getNullSentinel()) e.typeName = "null";
        else if (v->isString(ctx)) e.typeName = "string";
        else if (v->isBoolean(ctx)) e.typeName = "boolean";
        else if (v->isInteger(ctx) || v->isDouble(ctx)) e.typeName = "number";
        else if (!isObject) e.typeName = "function";
        else {
            e.typeName = "Object";
            if (const proto::ProtoString* ck = JSSymbols::constructor(ctx)) {
                const proto::ProtoObject* c = v->getAttribute(ctx, ck, true);
                if (c && c != PROTO_NONE && !isPrimitive(ctx, c)) {
                    const std::string n = stringAttr(ctx, c, JSSymbols::name(ctx), false);
                    if (!n.empty()) e.typeName = n;
                }
            }
        }
        e.message = format(ctx, v);
        e.text = e.message;
    }
    e.description = e.text;
    if (!innermostIsUser) {
        e.description += " (thrown in code that is not one of your files; "
                         "stopped at your innermost frame)";
    } else if (kind == dh::ThrowKind::Runtime) {
        e.description += " (raised by the runtime: an operation or a built-in "
                         "function called on this line)";
    } else if (kind == dh::ThrowKind::Kernel) {
        e.description += " (raised by protoCore: an operation on this line)";
    }
    for (size_t i = stack.size(); i-- > 0;) {
        const dh::Activation& a = stack[i];
        const FileInfo& file = fileInfo(a.module ? a.module->debugFile : std::string());
        std::string where = file.isUserFile ? file.displayPath
                          : (a.module && !a.module->debugFile.empty() ? a.module->debugFile
                                                                      : std::string("<native>"));
        e.stackTrace += "    at " + displayName(a) + " (" + where + ":" +
                        std::to_string(dh::lineForPc(a.module, a.pc)) + ")\n";
    }
    return e;
}

} // namespace

int runDapSession(char* argv0, ProgramRunner runProgram) {
    // Never destroyed: the exit hook and the detached reader use it until the
    // process ends.
    g_session = new Session();
    return g_session->run(argv0, runProgram);
}

} // namespace protojs::dap
