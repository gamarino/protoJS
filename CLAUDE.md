<!-- gitnexus:start -->
# GitNexus — Code Intelligence

This project is indexed by GitNexus as **protoJS** (27817 symbols, 56015 relationships, 300 execution flows). Use the GitNexus MCP tools to understand code, assess impact, and navigate safely.

> If any GitNexus tool warns the index is stale, run `npx gitnexus analyze` in terminal first.

## Always Do

- **MUST run impact analysis before editing any symbol.** Before modifying a function, class, or method, run `gitnexus_impact({target: "symbolName", direction: "upstream"})` and report the blast radius (direct callers, affected processes, risk level) to the user.
- **MUST run `gitnexus_detect_changes()` before committing** to verify your changes only affect expected symbols and execution flows.
- **MUST warn the user** if impact analysis returns HIGH or CRITICAL risk before proceeding with edits.
- When exploring unfamiliar code, use `gitnexus_query({query: "concept"})` to find execution flows instead of grepping. It returns process-grouped results ranked by relevance.
- When you need full context on a specific symbol — callers, callees, which execution flows it participates in — use `gitnexus_context({name: "symbolName"})`.

## When Debugging

1. `gitnexus_query({query: "<error or symptom>"})` — find execution flows related to the issue
2. `gitnexus_context({name: "<suspect function>"})` — see all callers, callees, and process participation
3. `READ gitnexus://repo/protoJS/process/{processName}` — trace the full execution flow step by step
4. For regressions: `gitnexus_detect_changes({scope: "compare", base_ref: "main"})` — see what your branch changed

## When Refactoring

- **Renaming**: MUST use `gitnexus_rename({symbol_name: "old", new_name: "new", dry_run: true})` first. Review the preview — graph edits are safe, text_search edits need manual review. Then run with `dry_run: false`.
- **Extracting/Splitting**: MUST run `gitnexus_context({name: "target"})` to see all incoming/outgoing refs, then `gitnexus_impact({target: "target", direction: "upstream"})` to find all external callers before moving code.
- After any refactor: run `gitnexus_detect_changes({scope: "all"})` to verify only expected files changed.

## Never Do

- NEVER edit a function, class, or method without first running `gitnexus_impact` on it.
- NEVER ignore HIGH or CRITICAL risk warnings from impact analysis.
- NEVER rename symbols with find-and-replace — use `gitnexus_rename` which understands the call graph.
- NEVER commit changes without running `gitnexus_detect_changes()` to check affected scope.

## Tools Quick Reference

| Tool | When to use | Command |
|------|-------------|---------|
| `query` | Find code by concept | `gitnexus_query({query: "auth validation"})` |
| `context` | 360-degree view of one symbol | `gitnexus_context({name: "validateUser"})` |
| `impact` | Blast radius before editing | `gitnexus_impact({target: "X", direction: "upstream"})` |
| `detect_changes` | Pre-commit scope check | `gitnexus_detect_changes({scope: "staged"})` |
| `rename` | Safe multi-file rename | `gitnexus_rename({symbol_name: "old", new_name: "new", dry_run: true})` |
| `cypher` | Custom graph queries | `gitnexus_cypher({query: "MATCH ..."})` |

## Impact Risk Levels

| Depth | Meaning | Action |
|-------|---------|--------|
| d=1 | WILL BREAK — direct callers/importers | MUST update these |
| d=2 | LIKELY AFFECTED — indirect deps | Should test |
| d=3 | MAY NEED TESTING — transitive | Test if critical path |

## Resources

| Resource | Use for |
|----------|---------|
| `gitnexus://repo/protoJS/context` | Codebase overview, check index freshness |
| `gitnexus://repo/protoJS/clusters` | All functional areas |
| `gitnexus://repo/protoJS/processes` | All execution flows |
| `gitnexus://repo/protoJS/process/{name}` | Step-by-step execution trace |

## Self-Check Before Finishing

Before completing any code modification task, verify:
1. `gitnexus_impact` was run for all modified symbols
2. No HIGH/CRITICAL risk warnings were ignored
3. `gitnexus_detect_changes()` confirms changes match expected scope
4. All d=1 (WILL BREAK) dependents were updated

## Keeping the Index Fresh

After committing code changes, the GitNexus index becomes stale. Re-run analyze to update it:

```bash
npx gitnexus analyze
```

If the index previously included embeddings, preserve them by adding `--embeddings`:

```bash
npx gitnexus analyze --embeddings
```

To check whether embeddings exist, inspect `.gitnexus/meta.json` — the `stats.embeddings` field shows the count (0 means no embeddings). **Running analyze without `--embeddings` will delete any previously generated embeddings.**

> Claude Code users: A PostToolUse hook handles this automatically after `git commit` and `git merge`.

## CLI

| Task | Read this skill file |
|------|---------------------|
| Understand architecture / "How does X work?" | `.claude/skills/gitnexus/gitnexus-exploring/SKILL.md` |
| Blast radius / "What breaks if I change X?" | `.claude/skills/gitnexus/gitnexus-impact-analysis/SKILL.md` |
| Trace bugs / "Why is X failing?" | `.claude/skills/gitnexus/gitnexus-debugging/SKILL.md` |
| Rename / extract / split / refactor | `.claude/skills/gitnexus/gitnexus-refactoring/SKILL.md` |
| Tools, resources, schema reference | `.claude/skills/gitnexus/gitnexus-guide/SKILL.md` |
| Index, status, clean, wiki CLI commands | `.claude/skills/gitnexus/gitnexus-cli/SKILL.md` |

<!-- gitnexus:end -->

# protoCore GC Bridging Rules

protoJS embeds protoCore as its object model. Whenever a `ProtoObject*` needs to outlive an allocation boundary that the protoCore tracing GC cannot see — typically because a C++ lambda registered with the EventLoop or a thread pool has captured the pointer — you MUST use one of the two protoCore-supplied mechanisms below. Smuggling references through `setAttribute` on the JS-side global is an anti-pattern; do not introduce new sites that do it.

See `protoCore/DESIGN.md` § "Keeping ProtoObjects alive across allocation boundaries the GC cannot see" for the full rationale.

## Decision rule

| Object lifetime | Mechanism | Example |
|---|---|---|
| Process-perpetual (language vocabulary, prototypes, cached literals) | NULL `ProtoContext` allocation | An attribute name `Symbol`, the `Function.prototype` object, the `__bytecode_id__` symbol |
| Bounded async (microseconds to seconds) | `ProtoRootSet` (`wrapper->getRootSet()`) | A `d.then(cb)` callback held in a setImmediate / pool-worker continuation; the deferred returned by `protoCore.runInThread` |

The two mechanisms are complementary, not interchangeable. **NEVER try to "release" a NULL-context allocation** (it has no release path; that is the whole point) and **NEVER lean on `ProtoRootSet` for objects that are conceptually language vocabulary** (you would be paying a per-cycle scan cost for no benefit).

## Mechanism A — Perpetual via NULL ProtoContext

Pass `nullptr` as the `ProtoContext*` parameter through the entire allocation call chain. The Cell goes through `posix_memalign` directly; it is never on a thread freelist or in a context's young chain, and it lives for the entire process.

```cpp
// Single-shot strong symbol — already done by createSymbol(...) for you.
const proto::ProtoString* k =
    proto::ProtoString::createSymbol(ctx, "myAttribute");

// Manual perpetual allocation — only if you need a non-string Cell:
auto* permanent = new(/*ctx=*/nullptr) MyCell(/*ctor args*/);
```

**Critical invariant**: every Cell reachable from a perpetual root must itself be perpetual. A perpetual root holding a normal GC-managed reference is a use-after-free waiting to happen, because the GC sees no path to that child. In practice this means a single `nullptr` threaded through the construction call chain — `fromUTF8Bytes(nullptr, ...)` → `buildAVL(nullptr, ...)` → `new(nullptr) ProtoStringImplementation(...)`.

Where protoJS already does this for you:
* Every `ProtoString::createSymbol(ctx, name)` call is internally `is_strong=true` and routes through the perpetual path.
* Every `setAttribute(ctx, key, value)` on a heap String key auto-interns the key strongly via `SymbolTable::intern(... is_strong=true)`, also perpetual.

Don't override these — they're correct.

## Mechanism B — `ProtoRootSet` (transient pin / unpin)

For receivers of asynchronous callbacks, deferred values, in-flight worker arguments — anything whose JS-side reachability ends before the C++ continuation fires — pin and release through the wrapper's root set.

The wrapper exposes a single root set named `"protojs-async"`, lazy-created on first use and torn down by `~JSContextWrapper`:

```cpp
proto::ProtoRootSet* rs = wrapper->getRootSet();
auto cbHandle  = rs->add(callbackObj);
auto valHandle = rs->add(valueObj);

EventLoop::getInstance().enqueueCallback([wrapper, cbHandle, valHandle]() {
    auto* rs = wrapper->getRootSet();
    const proto::ProtoObject* cb  = rs->resolve(cbHandle);
    const proto::ProtoObject* val = rs->resolve(valHandle);
    rs->remove(cbHandle);
    rs->remove(valHandle);
    // dispatch...
});
```

The handle is `proto::ProtoRootSet::Handle` (a 64-bit integer with embedded generation), so capturing it by value into a C++ lambda is safe and cheap. Multiple outstanding pins are independent — a stale `remove` of a handle whose slot has been recycled is a silent no-op thanks to the generation check.

## Anti-patterns to refuse

If you find yourself reaching for any of these in new code, STOP and use the right mechanism instead:

* `wrapper->getNativeGlobal()->setAttribute(ctx, "__some_pending_thing__", obj)` to pin async state. The GC IS aware of the global, but every async op pays for a CAS-rebuild-swap of the entire mutable-object snapshot, contends with every other writer, and leaks state cross-embedder. Use `getRootSet()->add(obj)`.
* Custom thread-local registries that mirror `ProtoRootSet`. The wrapper already owns one; reuse it.
* "Pin until end of program" workarounds for things that should obviously be in `createSymbol`. If it's vocabulary, intern it.
* Conditional pinning ("pin only if GC ran") — there is no way to reliably detect that, and any GC race makes this incorrect.

## Verification

When you add code that captures a `ProtoObject*` into a C++ lambda registered with `EventLoop::enqueueCallback` or `CPUThreadPool::submit`, before merging:

1. Identify every `ProtoObject*` in the lambda's capture list.
2. For each, point at where it was either pinned via `getRootSet()->add(...)` or proven to be perpetual.
3. If neither, the lambda has a latent use-after-free — fix it.
