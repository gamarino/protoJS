#ifndef PROTOJS_CONSOLE_H
#define PROTOJS_CONSOLE_H

#include "headers/protoCore.h"

namespace protojs {

class Console {
public:
    /**
     * Build the console object and register it on the native global.
     * Updates globalObj in-place (protoCore persistent-update semantics).
     */
    static void init(proto::ProtoContext* ctx, const proto::ProtoObject*& globalObj);

private:
    static const proto::ProtoObject* log(proto::ProtoContext*, const proto::ProtoObject*,
                                         const proto::ParentLink*,
                                         const proto::ProtoList*,
                                         const proto::ProtoSparseList*);
    static const proto::ProtoObject* error(proto::ProtoContext*, const proto::ProtoObject*,
                                           const proto::ParentLink*,
                                           const proto::ProtoList*,
                                           const proto::ProtoSparseList*);
    static const proto::ProtoObject* warn(proto::ProtoContext*, const proto::ProtoObject*,
                                          const proto::ParentLink*,
                                          const proto::ProtoList*,
                                          const proto::ProtoSparseList*);
    static const proto::ProtoObject* time(proto::ProtoContext*, const proto::ProtoObject*,
                                          const proto::ParentLink*,
                                          const proto::ProtoList*,
                                          const proto::ProtoSparseList*);
    static const proto::ProtoObject* timeEnd(proto::ProtoContext*, const proto::ProtoObject*,
                                             const proto::ParentLink*,
                                             const proto::ProtoList*,
                                             const proto::ProtoSparseList*);
    static const proto::ProtoObject* timeLog(proto::ProtoContext*, const proto::ProtoObject*,
                                             const proto::ParentLink*,
                                             const proto::ProtoList*,
                                             const proto::ProtoSparseList*);
    static const proto::ProtoObject* assert_(proto::ProtoContext*, const proto::ProtoObject*,
                                             const proto::ParentLink*,
                                             const proto::ProtoList*,
                                             const proto::ProtoSparseList*);
    static const proto::ProtoObject* group(proto::ProtoContext*, const proto::ProtoObject*,
                                           const proto::ParentLink*,
                                           const proto::ProtoList*,
                                           const proto::ProtoSparseList*);
    static const proto::ProtoObject* dir(proto::ProtoContext*, const proto::ProtoObject*,
                                         const proto::ParentLink*,
                                         const proto::ProtoList*,
                                         const proto::ProtoSparseList*);
    static const proto::ProtoObject* trace(proto::ProtoContext*, const proto::ProtoObject*,
                                           const proto::ParentLink*,
                                           const proto::ProtoList*,
                                           const proto::ProtoSparseList*);
    static const proto::ProtoObject* count(proto::ProtoContext*, const proto::ProtoObject*,
                                           const proto::ParentLink*,
                                           const proto::ProtoList*,
                                           const proto::ProtoSparseList*);
};

/**
 * Minimal timing-API installer.  Registers `Date.now()` (returns whole
 * milliseconds since the Unix epoch) and `performance.now()` (returns
 * monotonic milliseconds with sub-millisecond precision since program
 * start).  Required by the standard benchmark suites and by any
 * ES-compliant code that times itself; previously absent in protoJS.
 *
 * Updates globalObj in-place.  Safe to call once at startup, after the
 * Console has been installed.
 */
class TimingAPIs {
public:
    static void init(proto::ProtoContext* ctx, const proto::ProtoObject*& globalObj);

private:
    static const proto::ProtoObject* dateConstructor(proto::ProtoContext*, const proto::ProtoObject*,
                                                      const proto::ParentLink*,
                                                      const proto::ProtoList*,
                                                      const proto::ProtoSparseList*);
    static const proto::ProtoObject* dateNow(proto::ProtoContext*, const proto::ProtoObject*,
                                              const proto::ParentLink*,
                                              const proto::ProtoList*,
                                              const proto::ProtoSparseList*);
    static const proto::ProtoObject* performanceNow(proto::ProtoContext*, const proto::ProtoObject*,
                                                     const proto::ParentLink*,
                                                     const proto::ProtoList*,
                                                     const proto::ProtoSparseList*);
};

} // namespace protojs

#endif // PROTOJS_CONSOLE_H
