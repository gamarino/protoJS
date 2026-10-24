#include "DebugHooks.h"
#include "../runtime/ProtoBytecodeModule.h"
#include "../runtime/QuickJSBytecodeExport.h"

#include <algorithm>

namespace protojs {
namespace debughooks {

bool g_enabled = false;

namespace {
InstructionHandler s_onInstruction = nullptr;
ModuleLoadedHandler s_onModuleLoaded = nullptr;
ThrowHandler s_onThrow = nullptr;
CatchHandler s_onCatch = nullptr;
thread_local std::vector<Activation> t_activations;
thread_local bool t_suppressed = false;
thread_local int t_internalTries = 0;
// The activation depth of each live CatchingNative, innermost last.
thread_local std::vector<size_t> t_catchingNatives;
} // namespace

void enable(InstructionHandler onInstruction, ModuleLoadedHandler onModuleLoaded) {
    s_onInstruction = onInstruction;
    s_onModuleLoaded = onModuleLoaded;
    g_enabled = true;
    protojs_set_statement_line_info(1);
}

void setExceptionHandlers(ThrowHandler onThrow, CatchHandler onCatch) {
    s_onThrow = onThrow;
    s_onCatch = onCatch;
}

void setSuppressed(bool s) { t_suppressed = s; }
bool suppressed() { return t_suppressed; }

std::vector<Activation>& activations() { return t_activations; }

void pushActivation(proto::ProtoContext* ctx, const ProtoBytecodeModule* module,
                    const proto::ProtoObject* thisObj, const proto::ProtoObject** pGlobalRoot,
                    const void* catchStack) {
    t_activations.push_back(Activation{ctx, module, thisObj, pGlobalRoot, 0, 0, -1, catchStack});
}

void popActivation() {
    if (!t_activations.empty()) t_activations.pop_back();
}

void onInstruction(proto::ProtoContext* ctx, int pc) {
    if (t_suppressed || t_activations.empty() || !s_onInstruction) return;
    Activation& a = t_activations.back();
    if (a.ctx != ctx) return;    // not a frame this stack describes
    a.pc = pc;
    s_onInstruction(ctx, t_activations);
}

void setPc(proto::ProtoContext* ctx, int pc) {
    if (!t_activations.empty() && t_activations.back().ctx == ctx) t_activations.back().pc = pc;
}

void onThrow(proto::ProtoContext* ctx, const proto::ProtoObject* value, ThrowKind kind) {
    if (t_suppressed || t_internalTries > 0 || t_activations.empty() || !s_onThrow || !ctx) return;
    s_onThrow(ctx, t_activations, value, kind);
}

void onCatch(const proto::ProtoObject* value, bool finallyOnly) {
    if (s_onCatch && !t_suppressed) s_onCatch(value, finallyOnly);
}

InternalTry::InternalTry() : on(g_enabled) {
    if (on) ++t_internalTries;
}
InternalTry::~InternalTry() {
    if (on) --t_internalTries;
}

CatchingNative::CatchingNative() : on(g_enabled) {
    if (on) t_catchingNatives.push_back(t_activations.size());
}
CatchingNative::~CatchingNative() {
    if (on && !t_catchingNatives.empty()) t_catchingNatives.pop_back();
}

bool throwWillBeCaught(const std::vector<Activation>& stack) {
    // The activations above the innermost catching native are the ones whose
    // exceptions reach it; it catches what they do not.
    const size_t floor = t_catchingNatives.empty() ? 0 : t_catchingNatives.back();
    for (size_t i = stack.size(); i-- > floor;) {
        if (activationCatches(stack[i])) return true;
        // An async function's body: the exception rejects its promise.
        if (stack[i].module && stack[i].module->isAsync) return true;
    }
    return !t_catchingNatives.empty();
}

void moduleLoaded(const ProtoBytecodeModule& root) {
    if (s_onModuleLoaded) s_onModuleLoaded(root);
}

int lineForPc(const ProtoBytecodeModule* m, int pc, bool* prologue) {
    if (prologue) *prologue = false;
    if (!m || m->debugLinePcs.empty() || pc < 0) return 0;
    const auto& pcs = m->debugLinePcs;
    auto it = std::upper_bound(pcs.begin(), pcs.end(), static_cast<uint32_t>(pc));
    const size_t i = it == pcs.begin() ? 0 : static_cast<size_t>(it - pcs.begin()) - 1;
    // Entry 0 is the function's own line: the instructions it covers come
    // before the first statement (hoisted declarations, parameter setup).
    if (prologue) *prologue = i == 0 && pcs.size() > 1;
    return m->debugLines[i];
}

} // namespace debughooks
} // namespace protojs
