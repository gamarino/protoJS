#ifndef PROTOJS_VISUALPROFILER_H
#define PROTOJS_VISUALPROFILER_H

#include "quickjs.h"
#include "Profiler.h"
#include <string>
#include <vector>

namespace protojs {

class VisualProfiler {
public:
    static void init(JSContext* ctx);
    
    // Visualization methods
    static JSValue exportProfile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue generateHTMLReport(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

private:
    static std::string generateChromeDevToolsFormat(const Profiler::ProfileEntry& entry);
    static std::string generateHTMLReportContent(const std::vector<Profiler::ProfileEntry>& entries);
};

} // namespace protojs

#endif // PROTOJS_VISUALPROFILER_H
