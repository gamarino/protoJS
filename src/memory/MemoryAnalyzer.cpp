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
    size_t mallocSize = JS_GetRuntimeOpaque(rt) ? 0 : 0; // Simplified
    
    JS_SetPropertyStr(ctx, usage, "rss", JS_NewInt64(ctx, mallocSize));
    JS_SetPropertyStr(ctx, usage, "heapTotal", JS_NewInt64(ctx, mallocSize));
    JS_SetPropertyStr(ctx, usage, "heapUsed", JS_NewInt64(ctx, mallocSize));
    JS_SetPropertyStr(ctx, usage, "external", JS_NewInt64(ctx, 0));
    
    return usage;
}

} // namespace protojs
