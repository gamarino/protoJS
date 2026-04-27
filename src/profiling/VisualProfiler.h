#ifndef PROTOJS_VISUALPROFILER_H
#define PROTOJS_VISUALPROFILER_H

#include "headers/protoCore.h"
#include "Profiler.h"
#include <string>
#include <vector>

namespace protojs {

/**
 * @brief Visualization extensions for the `profiler` module —
 *        exportProfile (Chrome DevTools JSON) and generateHTMLReport.
 *        Migrated to protoCore-native; attaches to the existing
 *        protojs::Profiler module on the global.
 */
class VisualProfiler {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);
};

} // namespace protojs

#endif // PROTOJS_VISUALPROFILER_H
