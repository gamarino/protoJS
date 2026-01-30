#include "VisualProfiler.h"
#include "../modules/fs/FSModule.h"
#include <sstream>
#include <fstream>
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
        return JS_ThrowTypeError(ctx, "exportProfile expects filename");
    }
    
    const char* filename = JS_ToCString(ctx, argv[0]);
    if (!filename) return JS_EXCEPTION;
    
    // Get profile from profiler
    JSValue profiler = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "profiler");
    std::string json = "{\"type\":\"CPUProfile\",\"startTime\":0,\"endTime\":0,\"nodes\":[],\"samples\":[],\"timeDeltas\":[]}";
    
    if (!JS_IsUndefined(profiler)) {
        JSValue getProfileFunc = JS_GetPropertyStr(ctx, profiler, "getProfile");
        if (JS_IsFunction(ctx, getProfileFunc)) {
            JSValue profile = JS_Call(ctx, getProfileFunc, profiler, 0, nullptr);
            
            if (!JS_IsException(profile)) {
                // Build Chrome DevTools format
                std::stringstream ss;
                ss << "{\n";
                ss << "  \"type\": \"CPUProfile\",\n";
                ss << "  \"startTime\": 0,\n";
                ss << "  \"endTime\": 0,\n";
                ss << "  \"nodes\": [\n";
                
                JSValue entries = JS_GetPropertyStr(ctx, profile, "entries");
                if (!JS_IsUndefined(entries) && JS_IsArray(ctx, entries)) {
                    uint32_t len;
                    JS_ToUint32(ctx, &len, JS_GetPropertyStr(ctx, entries, "length"));
                    
                    bool first = true;
                    for (uint32_t i = 0; i < len; i++) {
                        JSValue entry = JS_GetPropertyUint32(ctx, entries, i);
                        if (!JS_IsUndefined(entry)) {
                            if (!first) ss << ",\n";
                            first = false;
                            
                            JSValue nameVal = JS_GetPropertyStr(ctx, entry, "name");
                            JSValue durationVal = JS_GetPropertyStr(ctx, entry, "duration");
                            
                            const char* name = JS_ToCString(ctx, nameVal);
                            double duration = 0;
                            JS_ToFloat64(ctx, &duration, durationVal);
                            
                            ss << "    {\"id\":" << i << ",\"callFrame\":{\"functionName\":\"" 
                               << (name ? name : "unknown") << "\"},\"hitCount\":1}";
                            
                            if (name) JS_FreeCString(ctx, name);
                            JS_FreeValue(ctx, nameVal);
                            JS_FreeValue(ctx, durationVal);
                            JS_FreeValue(ctx, entry);
                        }
                    }
                }
                
                ss << "\n  ],\n";
                ss << "  \"samples\": [],\n";
                ss << "  \"timeDeltas\": []\n";
                ss << "}";
                
                json = ss.str();
                JS_FreeValue(ctx, entries);
            }
            
            JS_FreeValue(ctx, profile);
            JS_FreeValue(ctx, getProfileFunc);
        }
    }
    JS_FreeValue(ctx, profiler);
    
    // Write to file
    std::ofstream file(filename);
    if (file.is_open()) {
        file << json;
        file.close();
        JS_FreeCString(ctx, filename);
        return JS_NewBool(ctx, true);
    } else {
        JS_FreeCString(ctx, filename);
        return JS_ThrowTypeError(ctx, "Failed to write profile file");
    }
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
