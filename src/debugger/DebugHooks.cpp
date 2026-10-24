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
thread_local std::vector<Activation> t_activations;
thread_local bool t_suppressed = false;
} // namespace

void enable(InstructionHandler onInstruction, ModuleLoadedHandler onModuleLoaded) {
    s_onInstruction = onInstruction;
    s_onModuleLoaded = onModuleLoaded;
    g_enabled = true;
    protojs_set_statement_line_info(1);
}

void setSuppressed(bool s) { t_suppressed = s; }
bool suppressed() { return t_suppressed; }

std::vector<Activation>& activations() { return t_activations; }

void pushActivation(proto::ProtoContext* ctx, const ProtoBytecodeModule* module,
                    const proto::ProtoObject* thisObj, const proto::ProtoObject** pGlobalRoot) {
    t_activations.push_back(Activation{ctx, module, thisObj, pGlobalRoot, 0, 0, -1});
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
