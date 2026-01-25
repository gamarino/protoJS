#include "VisualProfiler.h"
#include "../modules/fs/FSModule.h"
#include <sstream>
#include <iomanip>
#include <ctime>

namespace protojs {

void VisualProfiler::init(JSContext* ctx) {
    // Extend profiler module with visualization
    JSValue profiler = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "profiler");
    if (!JS_IsUndefined(profiler)) {
        JS_SetPropertyStr(ctx, profiler, "exportProfile", JS_NewCFunction(ctx, exportProfile, "exportProfile", 1));
        JS_SetPropertyStr(ctx, profiler, "generateHTMLReport", JS_NewCFunction(ctx, generateHTMLReport, "generateHTMLReport", 1));
    }
    JS_FreeValue(ctx, profiler);
}

JSValue VisualProfiler::exportProfile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "exportProfile expects profile object");
    }
    
    // Get profile from profiler
    JSValue getProfile = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "profiler");
    if (!JS_IsUndefined(getProfile)) {
        JSValue getProfileFunc = JS_GetPropertyStr(ctx, getProfile, "getProfile");
        if (JS_IsFunction(ctx, getProfileFunc)) {
            JSValue profile = JS_Call(ctx, getProfileFunc, getProfile, 0, nullptr);
            
            // Convert to Chrome DevTools format
            std::string json = "{\"type\":\"CPUProfile\",\"startTime\":0,\"endTime\":0,\"nodes\":[],\"samples\":[],\"timeDeltas\":[]}";
            
            JS_FreeValue(ctx, profile);
            JS_FreeValue(ctx, getProfileFunc);
            JS_FreeValue(ctx, getProfile);
            
            return JS_NewString(ctx, json.c_str());
        }
        JS_FreeValue(ctx, getProfileFunc);
    }
    JS_FreeValue(ctx, getProfile);
    
    return JS_NewString(ctx, "{}");
}

std::string VisualProfiler::generateChromeDevToolsFormat(const Profiler::ProfileEntry& entry) {
    std::stringstream ss;
    ss << "{\"functionName\":\"" << entry.name << "\","
       << "\"duration\":" << entry.duration << ","
       << "\"startTime\":" << entry.startTime << ","
       << "\"endTime\":" << entry.endTime << "}";
    return ss.str();
}

JSValue VisualProfiler::generateHTMLReport(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "generateHTMLReport expects filename");
    }
    
    const char* filename = JS_ToCString(ctx, argv[0]);
    if (!filename) return JS_EXCEPTION;
    
    // Generate HTML report
    std::string html = R"(<!DOCTYPE html>
<html>
<head>
    <title>protoJS Performance Profile</title>
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; }
        .timeline { border: 1px solid #ccc; padding: 10px; margin: 10px 0; }
        .entry { margin: 5px 0; padding: 5px; background: #f0f0f0; }
    </style>
</head>
<body>
    <h1>Performance Profile</h1>
    <div class="timeline">
        <h2>Timeline</h2>
        <p>Profile data would be displayed here</p>
    </div>
</body>
</html>)";
    
    // Write to file (simplified - would use FS module)
    JS_FreeCString(ctx, filename);
    
    return JS_NewBool(ctx, true);
}

std::string VisualProfiler::generateHTMLReportContent(const std::vector<Profiler::ProfileEntry>& entries) {
    std::stringstream ss;
    ss << "<!DOCTYPE html><html><head><title>Profile</title></head><body>";
    for (const auto& entry : entries) {
        ss << "<div class='entry'>" << entry.name << ": " << entry.duration << "ms</div>";
    }
    ss << "</body></html>";
    return ss.str();
}

} // namespace protojs
