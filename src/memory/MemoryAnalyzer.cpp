#include "MemoryAnalyzer.h"
#include "../ProtoNativeModule.h"
#include "../ArrayElementsStorage.h"
#include "../ArrayPrototype.h"
#include "../JSContext.h"
#include <fstream>
#include <sstream>
#include <ctime>

namespace protojs {

std::vector<MemoryAnalyzer::HeapSnapshot> MemoryAnalyzer::snapshots;
bool MemoryAnalyzer::trackingAllocations = false;
MemoryAnalyzer::HeapSnapshot MemoryAnalyzer::trackingStartSnapshot;

namespace {

MemoryAnalyzer::HeapSnapshot captureSnapshot(proto::ProtoSpace* space) {
    MemoryAnalyzer::HeapSnapshot s;
    s.timestamp = std::time(nullptr);
    if (!space) {
        s.totalSize = 0;
        return s;
    }
    // protoCore tracks `heapSize` (total cells allocated from the OS,
    // in 64-byte blocks) and `freeCellsCount` (cells currently on the
    // global free list).  We expose the byte equivalents — they are
    // the closest analogue to Node.js's heapTotal / heapUsed.
    size_t heapBytes = static_cast<size_t>(space->heapSize) * 64u;
    size_t freeBytes = static_cast<size_t>(space->freeCellsCount) * 64u;
    size_t usedBytes = (heapBytes > freeBytes) ? (heapBytes - freeBytes) : 0;
    s.totalSize = heapBytes;
    s.memoryUsage["heapTotal"] = heapBytes;
    s.memoryUsage["heapUsed"]  = usedBytes;
    s.memoryUsage["heapFree"]  = freeBytes;
    s.objectCounts["cells"] = static_cast<size_t>(space->heapSize);
    s.objectCounts["freeCells"] = static_cast<size_t>(space->freeCellsCount);
    s.objectCounts["runningThreads"] =
        static_cast<size_t>(space->runningThreads.load());
    return s;
}

const proto::ProtoObject* snapshotToObject(proto::ProtoContext* ctx,
                                            const MemoryAnalyzer::HeapSnapshot& s) {
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    auto setLong = [&](const char* k, long long v) {
        const proto::ProtoString* sk = ctx->fromUTF8String(k)->asString(ctx);
        if (sk) obj->setAttribute(ctx, sk, ctx->fromInteger(v));
    };
    auto setObj = [&](const char* k, const proto::ProtoObject* v) {
        const proto::ProtoString* sk = ctx->fromUTF8String(k)->asString(ctx);
        if (sk) obj->setAttribute(ctx, sk, v);
    };
    setLong("timestamp", static_cast<long long>(s.timestamp));
    setLong("totalSize", static_cast<long long>(s.totalSize));

    const proto::ProtoObject* counts = ctx->newObject(/*mutable=*/true);
    for (const auto& kv : s.objectCounts) {
        const proto::ProtoString* k =
            ctx->fromUTF8String(kv.first.c_str())->asString(ctx);
        if (k) counts->setAttribute(ctx, k, ctx->fromInteger(static_cast<long long>(kv.second)));
    }
    setObj("objectCounts", counts);

    const proto::ProtoObject* mem = ctx->newObject(/*mutable=*/true);
    for (const auto& kv : s.memoryUsage) {
        const proto::ProtoString* k =
            ctx->fromUTF8String(kv.first.c_str())->asString(ctx);
        if (k) mem->setAttribute(ctx, k, ctx->fromInteger(static_cast<long long>(kv.second)));
    }
    setObj("memoryUsage", mem);
    return obj;
}

MemoryAnalyzer::LeakReport compareSnapshots(
        const MemoryAnalyzer::HeapSnapshot& before,
        const MemoryAnalyzer::HeapSnapshot& after) {
    MemoryAnalyzer::LeakReport r;
    r.totalLeakedSize = 0;
    for (const auto& kv : after.objectCounts) {
        size_t b = before.objectCounts.count(kv.first)
            ? before.objectCounts.at(kv.first) : 0;
        if (kv.second > b) {
            r.leakedTypes.push_back(kv.first);
            r.leakCounts[kv.first] = kv.second - b;
        }
    }
    for (const auto& kv : after.memoryUsage) {
        size_t b = before.memoryUsage.count(kv.first)
            ? before.memoryUsage.at(kv.first) : 0;
        if (kv.second > b) {
            r.leakSizes[kv.first] = kv.second - b;
            r.totalLeakedSize += (kv.second - b);
        }
    }
    return r;
}

const proto::ProtoObject* leakReportToObject(proto::ProtoContext* ctx,
                                              const MemoryAnalyzer::LeakReport& r) {
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    auto setLong = [&](const char* k, long long v) {
        const proto::ProtoString* sk = ctx->fromUTF8String(k)->asString(ctx);
        if (sk) obj->setAttribute(ctx, sk, ctx->fromInteger(v));
    };
    auto setObj = [&](const char* k, const proto::ProtoObject* v) {
        const proto::ProtoString* sk = ctx->fromUTF8String(k)->asString(ctx);
        if (sk) obj->setAttribute(ctx, sk, v);
    };
    setLong("totalLeakedSize", static_cast<long long>(r.totalLeakedSize));

    const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
    const proto::ProtoList* els = ctx->newList();
    for (const auto& s : r.leakedTypes) els = els->appendLast(ctx, ctx->fromUTF8String(s.c_str()));
    setArrayElements(ctx, arr, els);
    setObj("leakedTypes", arr);

    const proto::ProtoObject* counts = ctx->newObject(/*mutable=*/true);
    for (const auto& kv : r.leakCounts) {
        const proto::ProtoString* k = ctx->fromUTF8String(kv.first.c_str())->asString(ctx);
        if (k) counts->setAttribute(ctx, k, ctx->fromInteger(static_cast<long long>(kv.second)));
    }
    setObj("leakCounts", counts);

    const proto::ProtoObject* sizes = ctx->newObject(/*mutable=*/true);
    for (const auto& kv : r.leakSizes) {
        const proto::ProtoString* k = ctx->fromUTF8String(kv.first.c_str())->asString(ctx);
        if (k) sizes->setAttribute(ctx, k, ctx->fromInteger(static_cast<long long>(kv.second)));
    }
    setObj("leakSizes", sizes);
    return obj;
}

std::string generateChromeJson(const MemoryAnalyzer::HeapSnapshot& s) {
    std::stringstream ss;
    ss << "{\n  \"snapshot\": {\n"
          "    \"meta\": {\n"
          "      \"node_fields\": [\"type\", \"name\", \"id\", \"self_size\", \"edge_count\", \"trace_node_id\"],\n"
          "      \"node_types\": [[\"hidden\", \"array\", \"string\", \"object\", \"code\", \"closure\", \"regexp\", \"number\", \"native\", \"synthetic\"]],\n"
          "      \"edge_fields\": [\"type\", \"name_or_index\", \"to_node\"],\n"
          "      \"edge_types\": [[\"context\", \"element\", \"property\", \"internal\", \"hidden\", \"shortcut\", \"weak\"]],\n"
          "      \"trace_function_info_fields\": [\"function_name\", \"script_name\", \"script_id\", \"line\", \"column\"],\n"
          "      \"trace_node_fields\": [\"id\", \"function_info_index\", \"count\", \"size\", \"children\"]\n"
          "    },\n"
          "    \"node_count\": " << s.objectCounts.size() << ",\n"
          "    \"edge_count\": 0\n"
          "  },\n"
          "  \"nodes\": [],\n"
          "  \"edges\": [],\n"
          "  \"strings\": [],\n"
          "  \"trace_function_infos\": [],\n"
          "  \"trace_tree\": null\n"
          "}";
    return ss.str();
}

proto::ProtoSpace* spaceForCtx(proto::ProtoContext* ctx) {
    return ctx ? ctx->space : nullptr;
}

const proto::ProtoObject* takeHeapSnapshotImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    auto s = captureSnapshot(spaceForCtx(ctx));
    MemoryAnalyzer::snapshots.push_back(s);
    return snapshotToObject(ctx, s);
}

const proto::ProtoObject* detectLeaksImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !args || args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* a0 = args->getAt(ctx, 0);
    const proto::ProtoObject* a1 = args->getAt(ctx, 1);
    if (!a0 || !a1 || !a0->isInteger(ctx) || !a1->isInteger(ctx)) return PROTO_NONE;
    long long b = a0->asLong(ctx);
    long long a = a1->asLong(ctx);
    auto& sn = MemoryAnalyzer::snapshots;
    if (b < 0 || b >= (long long)sn.size() || a < 0 || a >= (long long)sn.size())
        return PROTO_NONE;
    return leakReportToObject(ctx, compareSnapshots(sn[b], sn[a]));
}

const proto::ProtoObject* exportSnapshotImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !args || args->getSize(ctx) < 2) return PROTO_FALSE;
    const proto::ProtoObject* a0 = args->getAt(ctx, 0);
    const proto::ProtoObject* a1 = args->getAt(ctx, 1);
    if (!a0 || !a1 || !a0->isInteger(ctx) || !a1->isString(ctx)) return PROTO_FALSE;
    long long idx = a0->asLong(ctx);
    auto& sn = MemoryAnalyzer::snapshots;
    if (idx < 0 || idx >= (long long)sn.size()) return PROTO_FALSE;
    std::string filename;
    a1->asString(ctx)->toUTF8String(ctx, filename);
    std::ofstream f(filename);
    if (!f.is_open()) return PROTO_FALSE;
    f << generateChromeJson(sn[idx]);
    f.close();
    return PROTO_TRUE;
}

const proto::ProtoObject* getMemoryUsageImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    auto s = captureSnapshot(spaceForCtx(ctx));
    const proto::ProtoObject* usage = ctx->newObject(/*mutable=*/true);
    auto setLong = [&](const char* k, long long v) {
        const proto::ProtoString* sk = ctx->fromUTF8String(k)->asString(ctx);
        if (sk) usage->setAttribute(ctx, sk, ctx->fromInteger(v));
    };
    // Node.js-ish field names mapped to protoCore equivalents.
    setLong("rss",       static_cast<long long>(s.totalSize));
    setLong("heapTotal", static_cast<long long>(s.totalSize));
    setLong("heapUsed",  static_cast<long long>(s.memoryUsage["heapUsed"]));
    setLong("external",  0);
    return usage;
}

const proto::ProtoObject* startAllocationTrackingImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (MemoryAnalyzer::trackingAllocations) return PROTO_FALSE;
    MemoryAnalyzer::trackingAllocations = true;
    MemoryAnalyzer::trackingStartSnapshot = captureSnapshot(spaceForCtx(ctx));
    return PROTO_TRUE;
}

const proto::ProtoObject* stopAllocationTrackingImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!MemoryAnalyzer::trackingAllocations) return PROTO_FALSE;
    MemoryAnalyzer::trackingAllocations = false;
    auto end = captureSnapshot(spaceForCtx(ctx));
    return leakReportToObject(ctx,
        compareSnapshots(MemoryAnalyzer::trackingStartSnapshot, end));
}

}  // namespace

const proto::ProtoObject* MemoryAnalyzer::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    static const NativeEntry entries[] = {
        {"takeHeapSnapshot",        takeHeapSnapshotImpl},
        {"detectLeaks",             detectLeaksImpl},
        {"exportSnapshot",          exportSnapshotImpl},
        {"getMemoryUsage",          getMemoryUsageImpl},
        {"startAllocationTracking", startAllocationTrackingImpl},
        {"stopAllocationTracking",  stopAllocationTrackingImpl},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, entries, 6);
    if (!mod) return globalObj;
    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "memory", mod);
}

} // namespace protojs
