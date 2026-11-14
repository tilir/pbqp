# Development guide

## Sources of truth

Keep mandatory development rules here and detailed design/verification in:

- [Architecture](doc/architecture.md): responsibilities and dependency boundaries.
- [Solvers](doc/solvers.md): algorithm, numeric, and progress contracts.
- [bnbopt](doc/bnbopts.md): structural assumptions, capacity proofs, budgets.
- [Verification](doc/verification.md): required regression coverage, builds,
  sanitizers, CI, and reproducible Google Benchmark commands.
- [C API](include/pbqp.h), [C++ wrapper](include/pbqp.hpp): public contracts.
- [API version policy](doc/c_api.md), [programmer's guide](doc/programmers_guide.md),
  [format](doc/format.md), [generation](doc/generation.md): user documentation.

Update the owning document when behavior changes; do not duplicate its file
catalogue, algorithms, or benchmark instructions here. Keep README short.

## Language and organization

- Keep `include/pbqp.h` strictly C-compatible, implemented in C++23. Compile the
  C interoperability test as C. Maintain `pbqp.hpp` as a public header-only C++23
  client of the C boundary, not an export of internal implementation types.
- Write all documentation and code comments in English. Start every source and
  header with its purpose, SPDX `GPL-3.0-only`, and a reference to `LICENSE`.
- Use Clang 21.1 (`clang-21`, `clang++-21`) for development, with build tree
  `build-clang-modules`. Select compilers when configuring, never force them in
  project settings or change one inside an existing differently configured tree.
- Use CMake 3.28+, Ninja, CTest, and `clang-scan-deps`. No handwritten Makefile.
- Keep internal C++ in non-exported `.cppm` partitions of module `pbqp`, inside
  namespace `pbqp`. The primary interface exports no internal C++ names. Only
  C API declarations/definitions belong globally; qualify helpers with `pbqp::`
  and do not introduce global using-declarations or `pbqp_detail`.
- Tool support uses `pbqp_tools`; executable helpers use an anonymous namespace.
  Keep global `main`. Small implementations and short accessors may live directly
  in their module partitions; keep longer methods nearby.
- Include the C API and directly needed standard headers in the global module
  fragment. Do not attach standard headers to a named module, rely on transitive
  imports, or require `import std`. Import private partitions as `import :name`
  only from their owning module. Do not export internals for tools or tests.
- C API definitions belong in `.cpp` module implementation units with explicit
  `extern "C"`. Keep oneTBB headers/calls exclusively in `src/search/parallel.cpp`;
  never suppress GCC module-serialization errors or export private helpers.
- `core` and generic `search` must not import concrete solvers. B&B orchestration
  may compose heuristic seeds; bounds and workers remain independent of them.
  `src/solvers/bnb` owns baseline bounds and the engine shared with bnbopt.
- Use C++ streams, standard containers/ownership, and const-correct views. No C
  stdio/allocation in C++ and no `const_cast` exposing mutable const storage.
- Use `typename` for template type parameters. Do not introduce helper macros;
  prefer functions, templates, and typed constants.
- Format C, C++, and `.cppm` with `clang-format-21` and root `.clang-format` only
  (LLVM style, two spaces, no tabs).

## CMake and dependencies

- Each component owns explicit source lists and propagates library/format lists
  to its parent. No source globs. Reuse exact lists for target sources and
  `pbqp_register_format_sources`; collect all before creating formatter targets.
  Register tests/examples/benchmarks for formatting even when disabled.
- Register modules with `FILE_SET CXX_MODULES`: primary `pbqp` and its partitions
  PRIVATE; generator interface `pbqp.generator` PUBLIC. Compile modules once.
- Keep shared target options, formatting, and documentation setup in `cmake/`.
  Keep binaries/libraries at the build root; compiler selection is not a target
  option. Do not turn the C interoperability test into C++.
- Link installed oneTBB through `find_package(TBB CONFIG REQUIRED COMPONENTS tbb)`
  and `TBB::tbb`. Google Benchmark is optional through `PBQP_BUILD_BENCHMARKS`;
  use `find_package(benchmark CONFIG REQUIRED)` only when enabled. Register cases
  with `RegisterBenchmark` and explicit main, without macros or automatic downloads.
- If a local tool is missing, ask the user to install it. Do not download/install
  tools autonomously. CI dependency installation stays explicit in workflows.
- IWYU is deferred: 0.25 mishandles named modules. Do not claim coverage by
  generated header mirrors, diagnostic suppression, or omitting C++ sources.

## Ownership, errors, and stable API

- C API 1.x preserves existing signatures, enum numbers, public layouts/field
  types, and ownership contracts. Breaking changes require a major version;
  additions use minor versions and compatible fixes patch versions. Keep header
  constants, `pbqp_get_api_version`, project VERSION, and shared SOVERSION aligned.
  Use enum constants, not version macros. File-format versioning is independent.
- Keep exactly one dedicated entry per solver with shared `pbqp_solve_options`,
  plus `pbqp_solve` enum dispatch. No `_with_options`/callback-specific variants.
  NULL options select one thread with no callbacks; thread limits are positive
  and int-representable. Heuristics use one thread and reject progress callbacks.
- Public writes/solvers validate inputs themselves. Pointer validity and allocated
  lengths remain the caller's responsibility. Borrow input unchanged only during
  the call; returned solution options must have independent ownership.
- Free only unchanged parser-returned problems with `pbqp_problem_free`, and only
  unchanged solver-returned solutions with `pbqp_solution_free`, apart from NULL.
  Never pass caller-owned views to these deleters.
- No exception escapes C. Use shared `api_call` translation: bad_alloc to NO_MEMORY,
  length_error to OVERFLOW, other standard runtime/scheduler exceptions to
  INTERNAL_ERROR. Do not duplicate allocation checks in algorithms or add
  `-fno-exceptions`. Use vectors for owned arrays and unique_ptr for owned objects.
- Clear owned outputs to NULL and structured evaluation output on failure. Error
  records need no initialization; always NUL-terminate messages. Success clears
  line/message[0]; failure reports line when known, otherwise zero.
- C++ callbacks capture exceptions before crossing C, suppress further callbacks,
  and rethrow only after the synchronous call finishes. Do not promise cancellation.
- Bindings must query API version before reading layouts, keep enum/layout maps
  synchronized, and copy native solutions before freeing them. Foreign handles
  need idempotent disposal, including aliases/finalizers and frozen Ruby wrappers.
  Closing any Ruby Problem alias closes the shared handle for every alias.
  Never close handles concurrently with operations on them.

## Numerical and search invariants

- `pbqp_cost_t` is float and `pbqp_cost_add` remains ordinary floating-point `+`.
  Input accepts finite costs and +INF, rejects NaN/-INF. Do not add checked,
  saturating, exact accumulation or fast-math/reassociation. OVERFLOW is for
  unrepresentable input numbers/sizes, not ordinary floating-point overflow.
- Explicit INF forbids a tuple; overflowed infinite accumulated costs can still
  be feasible. Keep reachability separate. Evaluate unaries in node ID order,
  then edges in stored order. OPTIMAL refers to that sequentially rounded policy.
- Naive retains the first lexicographic minimum. B&B returns any optimum and may
  break ties by scheduling. Heuristics always return FEASIBLE on success; a dead
  end is NO_SOLUTION. Only a proof justifies INFEASIBLE. Preserve empty solutions.
- Check cheap root bounds before seeding, and skip reduction when greedy already
  proves optimality. Heuristic failure is not infeasibility. Shared incumbent
  publication copies assignment under a mutex; pruning reads atomic cost and a
  separate feasible flag. Prune L >= G only when a feasible incumbent exists.
- Bounds must respect reference addition order; never prune on assigned partial
  cost alone or regroup suffix costs. Verify every original term before using
  pure-interference symmetry/capacity. Coalescing/R2 matrices need not preserve it.
- Optimized search maps positions to original node IDs. Capacity DPs process every
  original unary in order, preserve reachability, and combine overlapping bounds
  by maximum. Double pricing chooses auxiliary nonnegative integer weights only;
  it is never a pruning cost. Check quantization and capacity sums before use.
- Enumerate complete maximal independent sets directly for capacity. Partial
  catalogues are unsafe: discard them on any storage/set/work limit and retain
  valid weaker bounds. Keep all-subset reference enumeration strictly in tests
  for differential comparison; production has no catalogue-kind selector.
  Dynamic bitsets have no fixed node/register word-width cap, clear padding bits,
  reject bool/signed/cv-qualified words, and reuse destination storage.
- Preparation may allocate; solver DFS/backpropagation must not allocate, validate,
  or add exception handling. Build reduction fill matrices and worker scratch
  beforehand. Keep reduction mutation/index/cache invariants in doc/solvers.md.
- Schedule coarse prefixes with oneTBB fixed consumers, never tasks per DFS vertex
  or a custom thread pool. Preallocate queues and donation scratch; remove donated
  ranges from the donor. Cancel/wake waiters before joining on scheduler failure.
- Comment every explicit non-sequential atomic memory order with its publication
  pairing or why no publication is needed. Stale bounds/hints may cause extra
  work, never incorrect pruning. Join all workers before returning.
- Naive progress counts every tuple exactly, with mixed-radix thresholds even
  beyond machine integers. B&B progress is approximate: distribute subtree
  weights among children surviving lookahead, cache their bounds, exclude
  immediate cuts and empty incoming jobs, and transfer weights on donation.
  Reserve B&B's 100 until workers join. Serialize callbacks and batch merges;
  never lock progress per tuple/closure. No zero-percent notification or callback
  outliving the call. Preparation failures emit no progress. Keep the
  no-progress path free of lookahead storage and progress accounting.

## CLI, documentation, and verification

- Preserve named options, duplicate/unknown/inapplicable-option rejection, and
  documented exit codes. No positional solve/dump subcommands. Keep terminal
  output in tools; COST/NODE stdout must not change with heartbeat or verbose.
- `--verbose` describes cached preparation decisions outside DFS. Keep generic
  metadata in algorithms and bnbopt formatting separate; never rescan eligibility
  in the CLI or claim an incomplete catalogue is usable.
- Preserve PBQP 1 ordering/comments and float round trips with classic locale and
  max_digits10. Keep generator families and deterministic sampling contracts.
- Keep fixtures in `examples/pbqp` and runnable C/C++/Python/Ruby clients in
  `examples/programming`. Update all consumers when paths change. Native examples
  are optional; bindings use only standard runtimes and an explicit shared-library
  path. Do not force shared builds on native clients.
- Generate API reference from public-header Doxygen comments. `PBQP_BUILD_DOCS`
  enables `docs` with Doxygen 1.9+ and warnings as errors. No handwritten duplicate
  symbol reference. Keep integration examples in the programmer's guide.
- Run relevant existing tests and required format/doc checks. Checks stay active
  in Release. Preserve independent final-tuple and canonical-prefix oracles,
  ownership/C ABI/binding regressions, exact naive/approximate B&B progress,
  and queue cancellation tests.
  Do not weaken an oracle by making it reuse the algorithm under test.
- Keep timing thresholds out of CTest and enormous exact fixture solves out of
  routine tests. Measure speed with Google Benchmark Release builds and real-time
  timing for parallel cases. Keep parsing/preflight checks outside measured loops.
- TSan claims require instrumented PBQP and oneTBB; use cmake/RunTSan.cmake with an
  existing checkout. Do not suppress reports from uninstrumented TBB to claim
  success. If ptrace prevents leak checking, disable it locally and report that
  limitation. Documentation-only changes need no rebuild or test rerun.
