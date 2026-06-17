#include "VisualProfiler.h"
#include "../ProtoNativeModule.h"
#include <fstream>
#include <sstream>

namespace protojs {

namespace {

// Build a Chrome DevTools-compatible CPU profile from the entries
// recorded by the Profiler module.  Output mirrors the previous
// QuickJS-side stringifier exactly (same field set, same ordering).
std::string buildChromeDevToolsJson() {
    std::stringstream ss;
    ss << "{\n";
    ss << "  \"type\": \"CPUProfile\",\n";
    ss << "  \"startTime\": 0,\n";
    ss << "  \"endTime\": 0,\n";
    ss << "  \"nodes\": [\n";
    bool first = true;
    int id = 0;
    for (const auto& e : Profiler::profileEntries) {
        if (!first) ss << ",\n";
        first = false;
        ss << "    {\"id\":" << id++ << ",\"callFrame\":{\"functionName\":\""
           << e.name << "\"},\"hitCount\":1}";
    }
    ss << "\n  ],\n";
    ss << "  \"samples\": [],\n";
    ss << "  \"timeDeltas\": []\n";
    ss << "}";
    return ss.str();
}

bool argFilename(proto::ProtoContext* ctx, const proto::ProtoList* args,
                  std::string& out) {
    if (!ctx || !args || args->getSize(ctx) == 0) return false;
    const proto::ProtoObject* a = args->getAt(ctx, 0);
    if (!a || !a->isString(ctx)) return false;
    a->asString(ctx)->toUTF8String(ctx, out);
    return true;
}

const proto::ProtoObject* exportProfileImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string filename;
    if (!argFilename(ctx, args, filename)) return PROTO_FALSE;
    std::ofstream file(filename);
    if (!file.is_open()) return PROTO_FALSE;
    file << buildChromeDevToolsJson();
    file.close();
    return PROTO_TRUE;
}

const proto::ProtoObject* generateHTMLReportImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string filename;
    if (!argFilename(ctx, args, filename)) return PROTO_FALSE;
    std::stringstream ss;
    ss << "<!DOCTYPE html>\n<html>\n<head>\n"
          "    <title>protoJS Performance Profile</title>\n"
          "    <style>\n"
          "        body { font-family: Arial, sans-serif; margin: 20px; }\n"
          "        .entry { margin: 5px 0; padding: 5px; background: #f0f0f0; }\n"
          "    </style>\n"
          "</head>\n<body>\n"
          "    <h1>Performance Profile</h1>\n"
          "    <div class='timeline'>\n";
    for (const auto& e : Profiler::profileEntries) {
        ss << "        <div class='entry'>" << e.name << ": "
           << e.duration << "ms</div>\n";
    }
    ss << "    </div>\n</body>\n</html>";
    std::ofstream file(filename);
    if (!file.is_open()) return PROTO_FALSE;
    file << ss.str();
    file.close();
    return PROTO_TRUE;
}

}  // namespace

const proto::ProtoObject* VisualProfiler::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    const proto::ProtoString* profKey =
        ctx->fromUTF8String("profiler")->asString(ctx);
    if (!profKey) return globalObj;
    const proto::ProtoObject* profilerObj = globalObj->getAttribute(ctx, profKey, false);
    if (!profilerObj || profilerObj == PROTO_NONE) return globalObj;

    // Add the two visualisation methods directly onto the existing
    // profiler module object via the standard helper.
    const proto::ProtoObject* updated =
        ProtoNativeModule::addMethod(ctx, profilerObj, "exportProfile",
                                      exportProfileImpl);
    updated = ProtoNativeModule::addMethod(ctx, updated, "generateHTMLReport",
                                            generateHTMLReportImpl);
    if (updated && updated != profilerObj) {
        // Re-bind on the global if the profiler object was immutable
        // (the buildModule path returns a non-mutable proto, so
        // addMethod returns a fresh updated object that we must re-pin).
        globalObj = globalObj->setAttribute(ctx, profKey, updated);
    }
    return globalObj;
}

} // namespace protojs
