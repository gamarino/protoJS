// Microbenchmark: compare three storage strategies for a JS array under
// `for (let i = 0; i < N; i++) arr.push(i)`:
//
//   A) Current path: setAttribute on a growing mutable ProtoObject
//      using STRING-keyed indices ("0", "1", ...) — what arrayPush does
//      today after our optimisations (1 setAttribute(idx) +
//      1 setAttribute(length)).
//
//   B) Native-list path: a ProtoList grown by appendLast.  This is the
//      core operation the proposed redesign would do per push.
//
//   C) Hybrid (proposed redesign): a mutable ProtoObject whose single
//      `__elements__` attribute holds a ProtoList grown by appendLast,
//      plus a `length` attribute kept in sync.  This is exactly what
//      arrayPush would compile to under the redesign.
//
// Tagged [.bench] so it does NOT run by default in ctest (which has a
// 60 s budget).  Run manually with:
//
//   ./build/tests/protojs_tests "[bench]"
//
// Output prints absolute milliseconds — read off the ratios to validate
// the redesign before committing to a multi-file refactor.

#include <catch2/catch_all.hpp>
#include <chrono>
#include <cstdio>
#include "../../src/JSContext.h"
#include "../../src/runtime/ProtoInterpreter.h"
#include "../../src/JSSymbols.h"
#include <protoCore.h>

using namespace protojs;
using clock_t_ = std::chrono::steady_clock;

namespace {

double ms_since(clock_t_::time_point t0) {
    auto t1 = clock_t_::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

}  // namespace

TEST_CASE("Array storage microbench (push N elements)", "[.bench]") {
    JSContextWrapper wrapper;
    proto::ProtoContext* ctx = wrapper.getProtoContext();
    REQUIRE(ctx != nullptr);

    constexpr int N = 100000;

    SECTION("A: current path — string-keyed setAttribute on mutable obj") {
        const proto::ProtoObject* arr = ctx->newObject(/*mutable=*/true);
        REQUIRE(arr != nullptr);
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        REQUIRE(lenKey != nullptr);

        auto t0 = clock_t_::now();
        for (int i = 0; i < N; i++) {
            const proto::ProtoString* idxKey =
                JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
            arr->setAttribute(ctx, idxKey, ctx->fromInteger(i));
        }
        // Bump length once at the end (matches our arrayPush fast-path).
        arr->setAttribute(ctx, lenKey, ctx->fromInteger(N));
        double elapsed = ms_since(t0);
        std::printf("[bench A] string-keyed setAttribute x%d: %.1f ms (%.3f us/op)\n",
                    N, elapsed, elapsed * 1000.0 / N);
    }

    SECTION("B: ProtoList::appendLast — pure native list growth") {
        const proto::ProtoList* list = ctx->newList();
        REQUIRE(list != nullptr);

        auto t0 = clock_t_::now();
        for (int i = 0; i < N; i++) {
            list = list->appendLast(ctx, ctx->fromInteger(i));
        }
        double elapsed = ms_since(t0);
        std::printf("[bench B] ProtoList::appendLast x%d: %.1f ms (%.3f us/op) — final size %lu\n",
                    N, elapsed, elapsed * 1000.0 / N,
                    static_cast<unsigned long>(list->getSize(ctx)));
    }

    SECTION("C: hybrid — mutable obj + __elements__ ProtoList") {
        const proto::ProtoObject* arr = ctx->newObject(/*mutable=*/true);
        REQUIRE(arr != nullptr);
        const proto::ProtoString* elementsKey =
            proto::ProtoString::createSymbol(ctx, "__elements__");
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        REQUIRE(elementsKey != nullptr);
        REQUIRE(lenKey != nullptr);

        // Seed with empty list.
        const proto::ProtoList* list = ctx->newList();
        arr->setAttribute(ctx, elementsKey, list->asObject(ctx));

        auto t0 = clock_t_::now();
        for (int i = 0; i < N; i++) {
            list = list->appendLast(ctx, ctx->fromInteger(i));
            arr->setAttribute(ctx, elementsKey, list->asObject(ctx));
        }
        // Bump length once at the end.
        arr->setAttribute(ctx, lenKey, ctx->fromInteger(N));
        double elapsed = ms_since(t0);
        std::printf("[bench C] hybrid (__elements__ list + setAttribute) x%d: %.1f ms (%.3f us/op)\n",
                    N, elapsed, elapsed * 1000.0 / N);
    }

    SECTION("D: hybrid lazy — only update __elements__ at end") {
        // Variant of C where we only publish the final list once — bounds
        // the absolute floor of the redesign if we batched setAttribute.
        const proto::ProtoObject* arr = ctx->newObject(/*mutable=*/true);
        REQUIRE(arr != nullptr);
        const proto::ProtoString* elementsKey =
            proto::ProtoString::createSymbol(ctx, "__elements__");
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);

        const proto::ProtoList* list = ctx->newList();

        auto t0 = clock_t_::now();
        for (int i = 0; i < N; i++) {
            list = list->appendLast(ctx, ctx->fromInteger(i));
        }
        arr->setAttribute(ctx, elementsKey, list->asObject(ctx));
        arr->setAttribute(ctx, lenKey, ctx->fromInteger(N));
        double elapsed = ms_since(t0);
        std::printf("[bench D] lazy publish (1 setAttribute total) x%d: %.1f ms (%.3f us/op)\n",
                    N, elapsed, elapsed * 1000.0 / N);
    }
}
