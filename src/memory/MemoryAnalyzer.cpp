#include "MemoryAnalyzer.h"
#include "../modules/fs/FSModule.h"
#include <sstream>
#include <iomanip>
#include <ctime>
#include <fstream>
#include <sys/resource.h>

namespace protojs {

std::vector<MemoryAnalyzer::HeapSnapshot> MemoryAnalyzer::snapshots;
bool MemoryAnalyzer::trackingAllocations = false;
MemoryAnalyzer::HeapSnapshot MemoryAnalyzer::trackingStartSnapshot;

void MemoryAnalyzer::init(JSContext* ctx) {
    JSValue memAnalyzer = JS_NewObject(ctx);
    
    JS_SetPropertyStr(ctx, memAnalyzer, "takeHeapSnapshot", JS_NewCFunction(ctx, takeHeapSnapshot, "takeHeapSnapshot", 0));
    JS_SetPropertyStr(ctx, memAnalyzer, "detectLeaks", JS_NewCFunction(ctx, detectLeaks, "detectLeaks", 2));
    JS_SetPropertyStr(ctx, memAnalyzer, "exportSnapshot", JS_NewCFunction(ctx, exportSnapshot, "exportSnapshot", 2));
    JS_SetPropertyStr(ctx, memAnalyzer, "getMemoryUsage", JS_NewCFunction(ctx, getMemoryUsage, "getMemoryUsage", 0));
    JS_SetPropertyStr(ctx, memAnalyzer, "startAllocationTracking", JS_NewCFunction(ctx, startAllocationTracking, "startAllocationTracking", 0));
    JS_SetPropertyStr(ctx, memAnalyzer, "stopAllocationTracking", JS_NewCFunction(ctx, stopAllocationTracking, "stopAllocationTracking", 0));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "memory", memAnalyzer);
    JS_FreeValue(ctx, global_obj);
}

MemoryAnalyzer::HeapSnapshot MemoryAnalyzer::captureSnapshot(JSContext* ctx) {
    HeapSnapshot snapshot;
    snapshot.timestamp = std::time(nullptr);
    snapshot.totalSize = 0;
    
    // Get memory usage from QuickJS runtime
    JSRuntime* rt = JS_GetRuntime(ctx);
    JS_ComputeMemoryUsage(rt, &snapshot.jsMemoryUsage);
    
    // Extract detailed statistics
    snapshot.totalSize = snapshot.jsMemoryUsage.malloc_size;
    snapshot.memoryUsage["malloc"] = snapshot.jsMemoryUsage.malloc_size;
    snapshot.memoryUsage["memory_used"] = snapshot.jsMemoryUsage.memory_used_size;
    snapshot.memoryUsage["atoms"] = snapshot.jsMemoryUsage.atom_size;
    snapshot.memoryUsage["strings"] = snapshot.jsMemoryUsage.str_size;
    snapshot.memoryUsage["objects"] = snapshot.jsMemoryUsage.obj_size;
    snapshot.memoryUsage["properties"] = snapshot.jsMemoryUsage.prop_size;
    snapshot.memoryUsage["shapes"] = snapshot.jsMemoryUsage.shape_size;
    snapshot.memoryUsage["js_functions"] = snapshot.jsMemoryUsage.js_func_size;
    snapshot.memoryUsage["js_func_code"] = snapshot.jsMemoryUsage.js_func_code_size;
    snapshot.memoryUsage["binary_objects"] = snapshot.jsMemoryUsage.binary_object_size;
    
    // Object counts
    snapshot.objectCounts["objects"] = snapshot.jsMemoryUsage.obj_count;
    snapshot.objectCounts["arrays"] = snapshot.jsMemoryUsage.array_count;
    snapshot.objectCounts["fast_arrays"] = snapshot.jsMemoryUsage.fast_array_count;
    snapshot.objectCounts["functions"] = snapshot.jsMemoryUsage.js_func_count;
    snapshot.objectCounts["c_functions"] = snapshot.jsMemoryUsage.c_func_count;
    snapshot.objectCounts["strings"] = snapshot.jsMemoryUsage.str_count;
    snapshot.objectCounts["atoms"] = snapshot.jsMemoryUsage.atom_count;
    snapshot.objectCounts["properties"] = snapshot.jsMemoryUsage.prop_count;
    snapshot.objectCounts["shapes"] = snapshot.jsMemoryUsage.shape_count;
    snapshot.objectCounts["binary_objects"] = snapshot.jsMemoryUsage.binary_object_count;
    
    return snapshot;
}

JSValue MemoryAnalyzer::takeHeapSnapshot(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    HeapSnapshot snapshot = captureSnapshot(ctx);
    snapshots.push_back(snapshot);
    
    return snapshotToJSObject(ctx, snapshot);
}

JSValue MemoryAnalyzer::detectLeaks(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "detectLeaks expects two snapshots");
    }
    
    // Get snapshot indices from arguments
    int64_t beforeIdx, afterIdx;
    if (JS_ToInt64(ctx, &beforeIdx, argv[0]) < 0 || JS_ToInt64(ctx, &afterIdx, argv[1]) < 0) {
        return JS_ThrowTypeError(ctx, "detectLeaks expects two snapshot indices");
    }
    
    if (beforeIdx < 0 || beforeIdx >= static_cast<int64_t>(snapshots.size()) ||
        afterIdx < 0 || afterIdx >= static_cast<int64_t>(snapshots.size())) {
        return JS_ThrowTypeError(ctx, "Invalid snapshot index");
    }
    
    const HeapSnapshot& before = snapshots[beforeIdx];
    const HeapSnapshot& after = snapshots[afterIdx];
    
    LeakReport report = compareSnapshots(before, after);
    
    return leakReportToJSObject(ctx, report);
}

JSValue MemoryAnalyzer::exportSnapshot(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "exportSnapshot expects snapshot index and filename");
    }
    
    int64_t snapshotIdx;
    if (JS_ToInt64(ctx, &snapshotIdx, argv[0]) < 0) {
        return JS_ThrowTypeError(ctx, "exportSnapshot expects snapshot index");
    }
    
    if (snapshotIdx < 0 || snapshotIdx >= static_cast<int64_t>(snapshots.size())) {
        return JS_ThrowTypeError(ctx, "Invalid snapshot index");
    }
    
    const char* filename = JS_ToCString(ctx, argv[1]);
    if (!filename) return JS_EXCEPTION;
    
    const HeapSnapshot& snapshot = snapshots[snapshotIdx];
    std::string json = generateChromeDevToolsFormat(snapshot);
    
    // Write to file using FS module
    std::ofstream file(filename);
    if (file.is_open()) {
        file << json;
        file.close();
        JS_FreeCString(ctx, filename);
        return JS_NewBool(ctx, true);
    } else {
        JS_FreeCString(ctx, filename);
        return JS_ThrowTypeError(ctx, "Failed to write snapshot file");
    }
}

JSValue MemoryAnalyzer::getMemoryUsage(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue usage = JS_NewObject(ctx);
    
    // Get memory usage from QuickJS runtime
    JSRuntime* rt = JS_GetRuntime(ctx);
    JSMemoryUsage memUsage;
    JS_ComputeMemoryUsage(rt, &memUsage);
    
    JS_SetPropertyStr(ctx, usage, "rss", JS_NewInt64(ctx, memUsage.malloc_size));
    JS_SetPropertyStr(ctx, usage, "heapTotal", JS_NewInt64(ctx, memUsage.malloc_size));
    JS_SetPropertyStr(ctx, usage, "heapUsed", JS_NewInt64(ctx, memUsage.memory_used_size));
    JS_SetPropertyStr(ctx, usage, "external", JS_NewInt64(ctx, memUsage.binary_object_size));
    
    return usage;
}

JSValue MemoryAnalyzer::startAllocationTracking(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (trackingAllocations) {
        return JS_NewBool(ctx, false);
    }
    
    trackingAllocations = true;
    trackingStartSnapshot = captureSnapshot(ctx);
    
    return JS_NewBool(ctx, true);
}

JSValue MemoryAnalyzer::stopAllocationTracking(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!trackingAllocations) {
        return JS_NewBool(ctx, false);
    }
    
    trackingAllocations = false;
    HeapSnapshot endSnapshot = captureSnapshot(ctx);
    
    LeakReport report = compareSnapshots(trackingStartSnapshot, endSnapshot);
    
    return leakReportToJSObject(ctx, report);
}

MemoryAnalyzer::LeakReport MemoryAnalyzer::compareSnapshots(const HeapSnapshot& before, const HeapSnapshot& after) {
    LeakReport report;
    report.totalLeakedSize = 0;
    
    // Compare object counts
    for (const auto& pair : after.objectCounts) {
        size_t beforeCount = before.objectCounts.count(pair.first) ? before.objectCounts.at(pair.first) : 0;
        size_t afterCount = pair.second;
        
        if (afterCount > beforeCount) {
            report.leakedTypes.push_back(pair.first);
            report.leakCounts[pair.first] = afterCount - beforeCount;
        }
    }
    
    // Compare memory usage
    for (const auto& pair : after.memoryUsage) {
        size_t beforeSize = before.memoryUsage.count(pair.first) ? before.memoryUsage.at(pair.first) : 0;
        size_t afterSize = pair.second;
        
        if (afterSize > beforeSize) {
            report.leakSizes[pair.first] = afterSize - beforeSize;
            report.totalLeakedSize += (afterSize - beforeSize);
        }
    }
    
    return report;
}

std::string MemoryAnalyzer::generateChromeDevToolsFormat(const HeapSnapshot& snapshot) {
    std::stringstream ss;
    ss << "{\n";
    ss << "  \"snapshot\": {\n";
    ss << "    \"meta\": {\n";
    ss << "      \"node_fields\": [\"type\", \"name\", \"id\", \"self_size\", \"edge_count\", \"trace_node_id\"],\n";
    ss << "      \"node_types\": [[\"hidden\", \"array\", \"string\", \"object\", \"code\", \"closure\", \"regexp\", \"number\", \"native\", \"synthetic\"]],\n";
    ss << "      \"edge_fields\": [\"type\", \"name_or_index\", \"to_node\"],\n";
    ss << "      \"edge_types\": [[\"context\", \"element\", \"property\", \"internal\", \"hidden\", \"shortcut\", \"weak\"]],\n";
    ss << "      \"trace_function_info_fields\": [\"function_name\", \"script_name\", \"script_id\", \"line\", \"column\"],\n";
    ss << "      \"trace_node_fields\": [\"id\", \"function_info_index\", \"count\", \"size\", \"children\"]\n";
    ss << "    },\n";
    ss << "    \"node_count\": " << snapshot.objectCounts.size() << ",\n";
    ss << "    \"edge_count\": 0\n";
    ss << "  },\n";
    ss << "  \"nodes\": [],\n";
    ss << "  \"edges\": [],\n";
    ss << "  \"strings\": [],\n";
    ss << "  \"trace_function_infos\": [],\n";
    ss << "  \"trace_tree\": null\n";
    ss << "}";
    
    return ss.str();
}

JSValue MemoryAnalyzer::snapshotToJSObject(JSContext* ctx, const HeapSnapshot& snapshot) {
    JSValue obj = JS_NewObject(ctx);
    
    JS_SetPropertyStr(ctx, obj, "timestamp", JS_NewInt64(ctx, snapshot.timestamp));
    JS_SetPropertyStr(ctx, obj, "totalSize", JS_NewInt64(ctx, snapshot.totalSize));
    
    JSValue objectCounts = JS_NewObject(ctx);
    for (const auto& pair : snapshot.objectCounts) {
        JS_SetPropertyStr(ctx, objectCounts, pair.first.c_str(), JS_NewInt64(ctx, pair.second));
    }
    JS_SetPropertyStr(ctx, obj, "objectCounts", objectCounts);
    
    JSValue memoryUsage = JS_NewObject(ctx);
    for (const auto& pair : snapshot.memoryUsage) {
        JS_SetPropertyStr(ctx, memoryUsage, pair.first.c_str(), JS_NewInt64(ctx, pair.second));
    }
    JS_SetPropertyStr(ctx, obj, "memoryUsage", memoryUsage);
    
    return obj;
}

JSValue MemoryAnalyzer::leakReportToJSObject(JSContext* ctx, const LeakReport& report) {
    JSValue obj = JS_NewObject(ctx);
    
    JS_SetPropertyStr(ctx, obj, "totalLeakedSize", JS_NewInt64(ctx, report.totalLeakedSize));
    
    JSValue leakedTypes = JS_NewArray(ctx);
    for (size_t i = 0; i < report.leakedTypes.size(); i++) {
        JS_SetPropertyUint32(ctx, leakedTypes, i, JS_NewString(ctx, report.leakedTypes[i].c_str()));
    }
    JS_SetPropertyStr(ctx, obj, "leakedTypes", leakedTypes);
    
    JSValue leakCounts = JS_NewObject(ctx);
    for (const auto& pair : report.leakCounts) {
        JS_SetPropertyStr(ctx, leakCounts, pair.first.c_str(), JS_NewInt64(ctx, pair.second));
    }
    JS_SetPropertyStr(ctx, obj, "leakCounts", leakCounts);
    
    JSValue leakSizes = JS_NewObject(ctx);
    for (const auto& pair : report.leakSizes) {
        JS_SetPropertyStr(ctx, leakSizes, pair.first.c_str(), JS_NewInt64(ctx, pair.second));
    }
    JS_SetPropertyStr(ctx, obj, "leakSizes", leakSizes);
    
    return obj;
}

} // namespace protojs
