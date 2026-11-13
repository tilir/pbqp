# Development guide

## Project conventions

- Keep the public API strictly C-compatible in `include/pbqp.h`; implement it
  in C++23. The C test must compile as C and link against the implementation.
- Use CMake 3.28 or newer with Ninja and CTest. Named module dependencies
  are scanned by `clang-scan-deps`. Do not add a handwritten Makefile.
- Use Clang 21.1 (`clang-21` and `clang++-21`) for development builds.
  Keep its build tree in `build-clang-modules`; compiler selection belongs in the
  configure command, not in project-wide forced CMake settings. Do not change
  compilers inside an existing build tree configured with another compiler.
- Write all documentation and code comments in English.
- Start every source and header with a comment explaining its purpose and
  an SPDX `GPL-3.0-only` notice referring to `LICENSE`.
- Keep internal C++ declarations and implementations in non-exported module
  partitions (`module pbqp:name;`, stored as `.cppm`), inside namespace `pbqp`.
  Namespace organization and module linkage serve separate purposes: keep
  internal names scoped without exporting them or introducing `pbqp_detail`.
  The primary `pbqp` interface exports no internal C++ names. Only the public
  C API belongs in the global namespace. C API definitions qualify internal
  helpers with `pbqp::`; do not introduce global using-directives/declarations.
  Tool support remains in `pbqp_tools`, and executable-local helpers use an
  anonymous namespace. Executable entry points `main` remain global.
- Module definitions are compiled once, so small cohesive implementations can
  live directly in `.cppm` files without header-style ODR workarounds. Keep
  short accessors in their class definitions and longer methods nearby.
- Register module units with CMake `FILE_SET CXX_MODULES`. The primary `pbqp`
  unit and its internal partitions belong to a PRIVATE file set. The separate
  `pbqp.generator` interface is PUBLIC for tools and tests.
- Import private partitions as `import :name;` only from within module `pbqp`.
  Do not export the partitions to make internal helpers available to clients.
- Include the C API and standard headers in the global module fragment before
  the module declaration. Do not attach standard library headers to a named
  module, rely on transitive imports, or require `import std`. Each source must
  include standard headers for the library names it uses.
- C API definitions live in `.cpp` implementation units of module `pbqp`.
  Wrap their definitions explicitly in `extern "C"` so they match the global
  declarations from `pbqp.h`. The public header remains includable from C;
  callers do not need to import modules.
- Keep oneTBB task/scheduler headers and calls in `src/search/parallel.cpp`, a
  module implementation unit. GCC cannot serialize some TU-local TBB helpers
  from a partition; do not work around this by suppressing diagnostics or
  exporting internal names. Worker and planner declarations stay in partitions.
- Explain each explicit non-sequential atomic memory order next to its use:
  identify the paired publication/read or why no publication is required.
- Apply const correctness to views and accessors. Do not use `const_cast` to
  expose mutable storage from a const object.
- Use C++ streams for file and terminal I/O, and standard library ownership
  and containers internally. Avoid C stdio and allocation functions in C++ code.
- Do not introduce helper macros. Use functions, templates, and typed constants.
- Follow LLVM formatting using `clang-format-21` and the root `.clang-format`:
  two-space indentation, no tabs. Format C, C++, and module interface files.
  Do not use a different formatter version for automated changes.
- Keep the README short: purpose, build, basic usage, and documentation links.
  Put format details in `doc/format.md`, API contracts in the public header,
  and implementation guidance here. Keep these documents consistent.

## Source layout

See `doc/architecture.md` for subsystem responsibilities and dependency rules.
`core` and shared `search` must not import concrete solvers. B&B orchestration
may compose heuristic seeds; its bounds and workers remain independent of them.
Each implementation subdirectory owns explicit source lists and propagates
library/format aggregates to its parent; module partition names stay unchanged.

- Root `CMakeLists.txt`: project setup, shared configuration, and subdirectories.
- `src/CMakeLists.txt`, `tools/CMakeLists.txt`, `tests/CMakeLists.txt`: component
  targets and test registration. Keep target source lists with their sources.
- `include/CMakeLists.txt`: explicit public header list. Components reuse their
  exact source lists for targets and formatting, register absolute paths through
  `pbqp_register_format_sources`, and propagate `PBQP_FORMAT_SOURCES` to the root.
  Do not use source globs. Create formatter targets only after collecting all
  component lists. Tests register their files even with `BUILD_TESTING=OFF`.
- `cmake/TargetOptions.cmake`: common C++23, module scanning, warnings, and
  output-directory helpers. Keep executables and libraries at the build root
  so documented commands remain valid. The C ABI test is still compiled as C.
- `cmake/Formatting.cmake`: shared `format` and `check-format` targets.
- `include/pbqp.h`: public types, inline cost algebra, API declarations, and
  contracts for validation, ownership, diagnostics, and solution quality.
- `src/pbqp.cppm`: primary module interface with no exported internal names.
- `src/io/text_reader.cppm`: private `:text_reader` partition containing the stream
  tokenizer, comment handling, line tracking, and numeric parser.
- `src/io/text_writer.cppm`: private `:text_writer` partition for cost serialization.
- `src/core/problem.cppm`: private `:problem` partition containing structural
  validation and dimension checks.
- `src/api/problem_api.cpp`: C problem destruction and validation entry points.
- `src/api/io.cpp`: C text input/output entry points and error translation.
- `src/api/error.cppm`: private `:error` partition with non-throwing diagnostics.
- `src/core/failure.cppm`: private `:failure` partition for parsing/validation errors.
- `src/api/api_boundary.cppm`: shared C API exception translation, including
  allocation failures and container size limits.
- `src/core/ownership.cppm`: vector-backed problem and solution storage behind C
  views, with RAII ownership helpers.
- `src/core/evaluation.cppm`: shared reference evaluator for validated assignments.
- `src/search/progress.cppm`: exact percentage boundaries for mixed-radix enumeration
  without multiplying domain sizes into a fixed-width counter.
- `src/api/solution.cpp`: public assignment evaluator and solution destruction.
- `src/api/bruteforce.cpp`: validation and ownership boundary for exhaustive search.
- `src/solvers/naive/bruteforce.cppm`: direct single-thread lexicographic reference enumeration.
- `src/solvers/naive/bruteforce_parallel.cppm`: private exhaustive workers, ordered coarse jobs,
  and a final cost/lexicographic reduction without per-tuple shared-best locking.
- `src/api/branch_and_bound.cpp`: validation and ownership boundary for exact search.
- `src/solvers/bnb/branch_and_bound.cppm`: coarse prefix queue and per-worker orchestration.
- `src/solvers/bnb/bnb_bounds.cppm`: immutable minima, option order, checked regalloc symmetry,
  forward checking, and reference-order conditioned bounds. Keep construction
  as named preparation steps and individual bound terms as small helpers.
- `src/solvers/bnb/bnb_incumbent.cppm`: mutex-protected assignment and atomic cost/feasibility.
- `src/solvers/bnb/bnb_tasks.cppm`: adaptive viable-prefix frontier and large-domain batches.
- `src/solvers/bnb/bnb_worker.cppm`: private assignment/cursors/scratch/local best and iterative
  allocation-free DFS within one prefix subtree.
- `src/search/search_progress.cppm`: shared serialized mixed-radix covered-space counts.
- `src/search/search_tasks.cppm`: shared prefix descriptors/planning and atomic tickets.
- `src/search/parallel.cppm`: private synchronous TBB dispatch declaration.
- `src/search/parallel.cpp`: shared oneTBB arena/task-group implementation bridge.
- `src/api/greedy.cpp`: validation and ownership boundary for the greedy C API.
- `src/solvers/greedy/greedy.cppm`: private `:greedy` partition with `GreedySearch`, separating
  workspace allocation, node priorities, edge indexing, option scoring, and
  assignment. Preparation allocates all search storage; `run` does not allocate,
  backtrack, or claim optimality.
- `src/solvers/reduction/reduction_graph.cppm`: private `:reduction_graph` partition with owned
  costs, indexed active adjacency, frozen eliminated adjacency, and R2 fill.
  Swap-and-pop removal updates edge positions at the affected endpoint. Sort
  adjacency by edge ID once on freezing to preserve arithmetic order.
  Intermediate feasibility is independent of the float score.
- `src/solvers/reduction/reduction_worklist.cppm`: incremental low-degree, safe, and spill queues
  with cached directed edge contributions and batched node-priority refreshes.
- `src/solvers/reduction/reduction.cppm`: private `:reduction` partition with `ReductionSearch`.
  Preparation applies R0/R1/R2/RN and allocates fill matrices; `run` performs
  allocation-free reverse assignment and reference evaluation.
- `src/api/reduction.cpp`: validation and ownership boundary for the heuristic C API.
- `tools/pbqp.cpp`: named solver selection and solver CLI. Exception handling
  covers C++ argument parsing only; C API calls report statuses.
- `tools/command_line.cppm`: shared `pbqp.command_line` module for named options,
  help flags, duplicate detection, and positional input paths.
- `tools/generator.cppm`: public `pbqp.generator` module containing generator
  options, owning `RandomProblem`, and `ProblemGenerator` with implementations.
  `view() const noexcept` exposes a borrowed, read-only C problem view.
- `tools/generate.cpp`: generator CLI argument handling.
- `tests/test_pbqp.cpp`: examples, round trips, invalid input, arithmetic,
  diagnostics, quality, and comparison with independent recursive search.
- `tests/test_parallel_exact.cpp`: multiworker costs/naive ties, serialized progress,
  mixed domains, huge counts, no-seed cases, and independent solution ownership.
- `tests/test_search_internals.cpp`, `tests/search_test_support.cpp`: private
  preparation regressions through a test-only C++ linkage bridge. Compile the
  support implementation unit in `pbqp` so partitions stay PRIVATE and are not
  recompiled or exported for tests. Omit this bridge with `BUILD_TESTING=OFF`.
- `tests/test_reduction.cpp`: randomized low-degree differential tests, RN
  cache invalidation, sparse shape reconstruction, and ownership checks.
- `benchmarks/CMakeLists.txt`, `benchmarks/reduction.cpp`,
  `benchmarks/branch_and_bound.cpp`, `benchmarks/bruteforce.cpp`: optional Google
  Benchmark star/K4/subdivided-star scaling and exact-solver thread counts,
  enabled with `PBQP_BUILD_BENCHMARKS`. `benchmarks/root_bounds.cpp` covers
  root-solved homogeneous and one-option-center domains.
  Use `find_package(benchmark CONFIG REQUIRED)` only when enabled, linking
  `benchmark::benchmark`. Register cases with `RegisterBenchmark` and an explicit
  main, without helper macros. Register formatting sources even when disabled.
- `tests/test_c_api.c`: C compatibility and linkage checks.
- `tests/cli.cmake`: end-to-end command-line usage, help, and diagnostic checks.

## Public API contracts

A `pbqp_problem` can describe caller-owned arrays or be owned by the parser.
Solvers borrow the problem and all arrays only during a call, do not modify
or retain them, and return solutions with independently owned `options`.
Keep borrowed inputs alive and unchanged until the call returns.

Only `pbqp_read` results may be passed to `pbqp_problem_free`, apart from
`NULL`. Treat parsed objects and their arrays as read-only: preserve pointers,
counts, dimensions, and node/edge metadata. Internal vectors own the storage
behind these C views. Caller-owned views must never be passed to this deleter.

Only solver-returned solutions may be passed to `pbqp_solution_free`, apart
from `NULL`. Preserve their original `options` pointer. Solutions outlive
their input problems.

Public write and solver entry points validate input themselves. Validation
checks positive option counts, non-NULL arrays for positive counts, endpoint
bounds with `src < tgt`, duplicate edges, matrix dimensions, and cost values.
For a zero count, its array pointer may be NULL or non-NULL and is not read.
Actual pointer validity and allocation lengths cannot be inferred: callers
must supply readable arrays with at least the declared lengths.

No C++ exception may escape the C API. Parsing and validation may use internal
exceptions translated by the shared `api_call` boundary. Use `std::vector`
for owned arrays, including solver workspace and storage backing returned C
objects. Preparation may allocate and throw; translate `std::bad_alloc` to
`PBQP_NO_MEMORY` and `std::length_error` to `PBQP_OVERFLOW` only at the boundary.
Other standard runtime/scheduler exceptions become `PBQP_INTERNAL_ERROR`.
Do not duplicate allocation checks in solver logic. Retain `std::unique_ptr`
for individual owned objects. Do not add `-fno-exceptions`; modules use
consistent compiler options. Search uses status returns; do not add exceptions,
allocation, or validation inside search loops. Reduction graph construction
and R2 fill allocation belong to preparation, before reverse assignment.

For APIs returning owned objects, set output pointers to NULL on failure.
Clear structured evaluation outputs on failure. A supplied
`pbqp_error` needs no initialization. Its message must always be NUL-terminated;
truncate diagnostics if needed. Success clears `line` and `message[0]`.
Failures provide a diagnostic and a source line if known, otherwise zero.
Bytes after the message terminator are unspecified.

## Costs and solver results

`pbqp_cost_t` is `float`. Keep `pbqp_cost_add(a, b)` as an inline wrapper
returning ordinary `a + b`, even though it currently needs no abstraction.
Do not add checked, saturating, or exact accumulation. `PBQP_INF` is positive
infinity. Input costs may be finite or positive infinity; reject NaN and
negative infinity. Arithmetic overflow follows floating-point behavior and
does not return `PBQP_OVERFLOW`. That status remains useful for unrepresentable
input numbers and sizes.
Do not enable fast-math flags that discard infinity handling or permit
reassociation of reference-order additions.

Explicit INF terms forbid a tuple. Distinguish these from a feasible tuple
whose sum overflows to infinity. Preserve reference summation order: unary
costs in node ID order, then edge costs in stored order. The reference solver
enumerates tuples lexicographically and keeps the first minimum, including
when multiple costs compare equal. The empty problem has one empty solution
with cost zero.

Execution status and result quality are separate. A solver's `PBQP_OK` returns
a solution; inspect `quality` to distinguish `PBQP_SOLUTION_FEASIBLE` from
`PBQP_SOLUTION_OPTIMAL`. The reference solver sets OPTIMAL after exhaustive
search. `PBQP_INFEASIBLE` requires proof that no feasible assignment exists.
A heuristic failing to find an assignment returns `PBQP_NO_SOLUTION`, never
`PBQP_INFEASIBLE`. Greedy and reduction return FEASIBLE even when they happen to find an
optimum or solve the empty problem.

`pbqp_evaluate_assignment` validates inputs and indices, then reports both a
reference-order cost and an independent `is_feasible` flag. A forbidden tuple
is reported successfully with `is_feasible == 0`, not as problem-level
infeasibility. All solvers use its shared internal evaluator for reported
costs. OPTIMAL refers only to this sequentially rounded evaluation policy,
not an exact real sum. Regrouping float arithmetic can change the optimum;
merely recomputing the final assignment's cost does not prove optimality.

Greedy visits nodes by descending finite unary cost range (maximum minus
minimum), with node ID as the tie-breaker. For regalloc this is descending
spill cost. It scores unary costs plus edges to already assigned nodes in
stored edge order and keeps the first minimum allowed option. Respect source
rows and target columns even when the target is assigned before the source.
Edge indices are sorted once by the assignment step of their later endpoint
and then original edge index, so sparse search does not rescan every edge for
every node. Allocate scratch storage before the node/option loops. A dead end
terminates with NO_SOLUTION. On `02_greedy_exact.pbqp`, greedy must color nodes 1, 3, and 5
before spilling node 4, for total cost 40; its result quality remains FEASIBLE.

Branch-and-bound returns any reference-order optimum; ties can depend on
scheduling. Naive alone preserves the first lexicographic minimum. B&B assigns
nodes in ID order and visits options by unary cost, then ID. Shared immutable
bounds include unary, row, and matrix minima; each worker owns its assignment,
DFS cursors, allowed-option scratch, and local best solution. Forward checking
removes explicit-INF options against the fixed prefix, tightening independent
unary and pairwise minima. Sum those terms in original reference order, never
regrouping suffixes or pruning solely on assigned partial cost.

Check the root bound before seeding: explicit infeasibility closes the whole
space without running a heuristic. Try greedy next and compare its incumbent
against that root bound. Run the reduction seed only if improvement is still
possible; do not construct a reduction graph or R2 fill for a root already
proved optimal. Keep seed-attempt statistics available to private tests and
preserve deferred root progress reporting. Seed the shared incumbent from
successful heuristic runs. A heuristic
dead end is not infeasibility. Cost and a separate feasible flag are atomic;
copy the winning assignment under a mutex before publication. Frequent bound
checks never copy the incumbent assignment. Stale upper bounds allow extra work.
Prune `L >= G` whenever a feasible incumbent exists, including overflowed +INF.
Without an incumbent, infinity alone cannot discard a feasible subtree.

Detect register symmetry only after checking every original unary and matrix:
all nodes share a domain, registers cost zero, spill rows/columns are zero,
register diagonals INF, and other register pairs zero. Canonicalize new register
names in order of first appearance. Renaming preserves reference terms; do not
apply this optimization to arbitrary PBQP matrices or unequal domains.

Use an adaptive viable-prefix frontier with about 32 jobs per requested worker,
child batches for huge domains, and a bounded fan-out expansion budget.
Append singleton runs in place, check their resulting prefix once, and do not
charge them to the branching budget. A long forced prefix must not remove
requested concurrency when branching follows it. The
single-thread path uses the same planner but no scheduler. A oneTBB task_arena
and task_group run fixed consumers claiming whole subtrees from an atomic
index. Never create a task per DFS vertex or implement a custom thread pool.
Allocate worker scratch before search; TBB runtime allocations stay outside DFS.
Keep all input/worker storage alive through task-group completion. Report
closed planning prefixes only after preparation succeeds. See `doc/solvers.md`.

Parallel naive uses the same TBB dispatcher, prefix jobs, and coverage counter,
but no heuristic or bound can prune a tuple. Sort its jobs lexicographically;
an individual consumer receives increasing indices and retains its first local
minimum. After joining, merge worker results by reference cost and lexicographic
assignment, including feasible +/-INF costs. This preserves naive's first-minimum
contract. Keep the single-thread naive path direct and its no-progress observer
compiled away. The new entry point is `pbqp_solve_bruteforce_with_options`.

The reduction heuristic clones the problem and prefers R0/R1/R2 by node ID.
R1 adds a minimized leaf contribution to its neighbor; R2 adds minimized
contributions to a neighbor edge, allocating fill if needed. RN disconnects
one high-degree node without propagation, preferring conservatively available
nonzero options, then lowest option-zero cost, degree, and ID. Match LLVM's
reduction scheme, without depending on its regalloc-only domain assumptions.
Keep feasibility separate from overflowed intermediate float costs. Preserve
frozen eliminated-node costs/edges for reverse assignment; later mutations
must touch active adjacency only. R2 edge lookup scans the endpoint with smaller active degree; canonical matrix
orientation remains unchanged. Never scan and erase an active adjacency
vector on each disconnect. Keep per-endpoint edge positions and sort each
node's frozen edges by ID before propagation and backpropagation.
Maintain RN availability incrementally: cache each edge direction's worst
blocked count and unsafe-option flags, summing them per node. R1 feasibility
changes invalidate contributions on every incident edge, in both directions;
finite score-only changes invalidate the node's spill key. R2 fill/merge and
edge removal update both endpoints. Batch candidate refreshes until the graph
mutation is complete, and use immutable priority snapshots in ordered sets.
Do not rescan unrelated candidates or split exact search into components.
Final costs use the original reference
order. Always return FEASIBLE on success; failed reconstruction is NO_SOLUTION.
See `doc/solvers.md` for formulas and the deterministic NaN preference policy.

Input accepts decimal fractions and scientific notation. Keep the classic
locale for serialization and use `max_digits10` precision so finite float
costs round-trip. Preserve the PBQP 1 ordering and comment rules documented
in `doc/format.md`.

## Tools and integration

Use `add_subdirectory` and link the `pbqp` CMake target to embed the library.
C callers need the C++ runtime at final link time; CMake handles this.
The `pbqp_cli` target produces the `pbqp` executable. `pbqp_generator` is the
reusable generator library; `pbqp_generate` is its CLI. Both tools link the
shared `pbqp_command_line` library for argument parsing. The library links
installed oneTBB via `find_package(TBB CONFIG REQUIRED COMPONENTS tbb)` and
`TBB::tbb`; development systems and CI need `libtbb-dev`.

Invoke the solver as `pbqp --solve=naive INPUT`. `pbqp --help` lists available
solvers: `naive` selects exhaustive search, `bnb` exact branch-and-bound,
`greedy` cost-priority assignment, and `r1r2rn` reduction/backpropagation. Do not
reintroduce `solve` or `dump` positional subcommands. Problem dumping remains
available to library users through `pbqp_write`.
`--numthreads=N` is a positive, int-representable naive/B&B concurrency limit,
accepting either named-option spelling; default 1. The C options entry point is
`pbqp_solve_branch_and_bound_with_options`; NULL options select one thread and
no callback, and zero threads is invalid. Both options-based exact solvers use
`pbqp_solve_options`. Legacy exact entry points use one thread.

`--heartbeat` is a valueless flag for `naive` and `bnb`. It writes one flushed dot to
stderr for each completed integer percentage, from 1 through 100, followed by
a newline at completion. Count all tuples, including forbidden assignments.
The first dot is emitted after ceil(total / 100) visited tuples; no zero-percent
notification is sent. For branch-and-bound, count all tuples in fully searched
or pruned subtrees, including symmetry cuts and deferred planning events, so a
single cut may cross many percentage boundaries.
Small search spaces may cross several percentages on one tuple. Progress
counts covered space, not elapsed time or an ETA.

The library exposes `pbqp_solve_bruteforce_with_progress` with an optional
synchronous C callback, options-based parallel entry points, and the corresponding
`pbqp_solve_branch_and_bound_with_progress` entry point. Keep terminal output
in the CLI. Callbacks must not
throw or mutate the input. Progress scratch storage is allocated before
enumeration. The no-progress path uses a compiled-away observer, with no
per-tuple progress branch. Preserve exact boundaries even if the domain-size
product exceeds size_t or uint64_t; do not saturate the total or report fake
early percentages. The mixed-radix boundary is ceil(total * p / 100) - 1.
Naive retains its exact lexicographic `PercentProgress` boundaries. Parallel exact search uses
`SubtreeProgress` to add each closed prefix's suffix product in mixed radix,
without multiplying the full domain product. Out-of-order completion is safe;
a mutex serializes batch merges and callbacks. Each worker owns an accumulator;
flush it after 256 small closures, after a one-percent-sized cut, and at every
job boundary. Never take the shared progress mutex per tuple or DFS closure.
Allocate accumulators during preparation. Pending counts can delay notification
but never justify an early percentage; flush all tails before return.
Parallel callbacks may run on TBB workers
and must not throw or modify input; no callback outlives the synchronous call.

The solver CLI prints `COST <cost>` and zero-based `NODE <id> <option>` records.
Exit codes are 0 for success, 1 for errors, 2 for proven infeasibility, and
3 for a search that found no solution without an infeasibility proof.
Exhaustive search evaluates a number of tuples equal to the product of all
node option counts and is intended for small instances.

Generator options and families are described in `doc/generation.md`.
`--nodes=N --edges=M --seed=S --output=FILE` are always required. Modes:

- `--mode=random` (default): requires `--max-options=D`; quarter-step costs
  in [-10, 10] with a 10% chance of INF per entry; may be infeasible.
- `--mode=planted`: same domains/cost family, but selected unary and pairwise
  entries of a hidden assignment are forced finite. Guarantees feasibility.
- `--mode=regalloc`: requires `--registers=R`. Every node has R+1 options;
  spill is option 0 with a finite positive cost uniformly sampled from
  `--spill-cost-min` through `--spill-cost-max` (defaults 10, 100). Register
  unary costs are zero. Edge matrices are zero except for INF on register
  diagonals; spill's row and column stay zero. All-spill is always allowed.

Floyd sampling selects distinct pair ranks, with complement sampling for
dense graphs. Output is sorted by endpoints; mapping ranks takes O(M log N)
without a quadratic scan for sparse workloads. Maintain deterministic output
for a fixed seed/build; do not promise identical sampling across different
standard libraries or generator revisions.

Named options accept either `--name=value` or `--name value` in any order;
reject duplicates, unknown options, missing values, and invalid numbers.
Reject mode-inapplicable options rather than silently ignoring them. The
solver accepts `--` before positional paths starting with `--`. The generator
accepts no positional values. `pbqp_generate --help` describes all modes.

## Verification

With `clang-format-21` installed, CMake provides formatting targets:

```sh
cmake --build build-clang-modules --target format
cmake --build build-clang-modules --target check-format
```

`format` updates source files; `check-format` fails if formatting differs.
The formatting file list includes `.cppm` interfaces and the C API test.
The targets are available only when CMake finds `clang-format-21`.

`.github/workflows/ci.yml` runs on pushes, pull requests, and manual dispatch.
It uses Ubuntu 24.04, the official LLVM apt repository, Clang 21, Ninja, and
packaged oneTBB.
The Release job checks formatting; both Release and ASan/UBSan jobs build and
run CTest, including the C interoperability test. Sanitizer errors fail CI,
and leak checking remains enabled on GitHub's runner.

IWYU integration is deferred. IWYU 0.25 built against Clang 21 was tested:
it suggests textual includes of imported `.cppm` files and crashes on one
of the module consumers. Do not introduce generated header mirrors, suppress
these failures, or silently omit C++ files to claim full IWYU coverage.
Reconsider integration when named modules are supported reliably.

Run the relevant existing tests for behavior changes:

```sh
cmake -G Ninja -S . -B build-clang-modules -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang-21 -DCMAKE_CXX_COMPILER=clang++-21
cmake --build build-clang-modules
ctest --test-dir build-clang-modules --output-on-failure
```

Checks remain active in Release builds. Preserve the C interoperability test,
float round trips, and independent recursive comparisons on generated small
problems. Current example optima are 0, 40, and 80 for `01_basic.pbqp`, `02_greedy_exact.pbqp`,
and `03_greedy_inexact.pbqp`, respectively.
The recursive oracle evaluates complete tuples in reference summation order
and tracks feasibility separately from the best cost; it also covers ordinary
non-exact float sums and infinite accumulated values. Keep it independent of
the shared evaluator. Failed comparisons report the seed, dimensions, both
assignments/costs, and a retained failing problem dump. Compare B&B costs and
assignment feasibility, allowing different tied winners; naive still compares
the full lexicographic assignment. Test checks use
`std::source_location` so ordinary failures identify their source line.
Also preserve direct C API validation, ownership lifetime checks, partial-read
cleanup tests, I/O failures (including `/dev/full` when available), greedy
traps, and sparse graph generation without quadratic pair scans. Reduction
coverage includes R1 orientations, heterogeneous domains, R2 fill and existing
edges, RN dead ends, overflow feasibility, and input/solution ownership.
`06_too_long_for_any_exact.pbqp` runs only through greedy and r1r2rn in tests;
validate both assignments and reference-order costs without exact search.
Randomized trees, forests, and cycles use quarter-step costs small enough that
all intermediate sums are exact. Compare feasibility and optimum cost with
exhaustive search, without requiring identical tied tuples. Preserve input
arrays and verify solution storage after destroying the input. Include RN
regressions for neighbor feasibility and finite spill-priority changes.
Large stars and subdivided stars vary center ID and edge storage order; disconnected K4s exercise
repeated RN selection. Keep wall-clock thresholds out of ordinary CTest.
Heartbeat tests cover the C callback, unchanged solver output, the complete
1..100 notification sequence, empty and infeasible searches, and no early
progress on a timed-out 2^65-tuple search. Do not run `05_too_long_for_naive.pbqp`'s complete
2,176,782,336-tuple enumeration as a routine CTest entry.
Preserve greedy-seed regressions for dead ends, suboptimal initial solutions,
reference rounding, tied optimum costs, and infinite accumulated seed costs.
Compare branch-and-bound with both naive and the independent oracle on all
small generator families, including fractional spill costs and extreme float
values. Preserve targeted negative-edge and cancellation regressions; a
reassociated or partial-only bound must not discard a better solution.
Check 100 callbacks on huge feasible and root-infeasible 2^65-tuple spaces
that branch-and-bound can close quickly.

Google Benchmark scalability cases are optional and are not CTest entries.
Install the Google Benchmark development package (for example,
`libbenchmark-dev`), or make its CMake package available through
`CMAKE_PREFIX_PATH`. The default library/tool/test build has no dependency on
Google Benchmark and never downloads it automatically. Build PBQP in Release
and keep the same Google Benchmark package and measurement conditions when
comparing revisions. The system `libbenchmark-dev` package is sufficient.

```sh
cmake -S . -B build-clang-modules -DPBQP_BUILD_BENCHMARKS=ON
cmake --build build-clang-modules --target pbqp_benchmark_reduction
./build-clang-modules/pbqp_benchmark_reduction --benchmark_repetitions=3 \
  --benchmark_out=/tmp/pbqp-reduction.json --benchmark_out_format=json
```

`Reduction/Star` and `Reduction/K4` cover 8k, 16k, 32k, and 64k nodes.
`Reduction/SubdividedStar/HubLow` and `/HubHigh` cover 4k/8k/16k/32k arms,
with ordered or shuffled edge storage. HubLow forces repeated R2 fill beside
an active high-degree hub, exposing quadratic adjacency lookup. Google
Benchmark controls iterations, repetitions, timing, statistics, and output.
Use `--benchmark_list_tests` to list cases and `--benchmark_filter=Reduction/K4`
to select a family. Each case reports node/edge counters, nodes processed per
second, and an empirical complexity fit. Consult the
[Google Benchmark user guide](https://google.github.io/benchmark/user_guide.html)
for the framework's command-line options.

`pbqp_benchmark_bnb` loads examples 07 and 08 from the checkout root. Example 07
compares 1/2/4/8 requested threads; example 08 compares 1/4/16 with heartbeat
enabled and disabled, covering progress contention. Both use `UseRealTime`, so
worker CPU time is not mistaken for elapsed solve time. Parsing and an optimum
and assignment check are untimed. The benchmark heartbeat uses a no-op callback
to measure progress accounting without terminal I/O. Run:

```sh
cmake --build build-clang-modules --target pbqp_benchmark_bnb
./build-clang-modules/pbqp_benchmark_bnb --benchmark_repetitions=3 \
  --benchmark_out=/tmp/pbqp-bnb.json --benchmark_out_format=json
```

`pbqp_benchmark_naive` compares 1/2/4/8 threads on a fixed binary 20-node
problem, evaluating all 2^20 tuples per solve. It reports wall time and tuples
per second; setup and an optimum check are outside timing:

```sh
cmake --build build-clang-modules --target pbqp_benchmark_naive
./build-clang-modules/pbqp_benchmark_naive --benchmark_repetitions=3
```

`pbqp_benchmark_root_bounds` measures zero-cost three-node paths with domain
sizes 64/128/256 and either an equally sized or one-option center. Root proofs
must bypass cubic reduction work and quadratic fill amplification. Private
regressions assert the seed-attempt flags, with no wall-clock thresholds.

```sh
cmake --build build-clang-modules --target pbqp_benchmark_root_bounds
./build-clang-modules/pbqp_benchmark_root_bounds --benchmark_repetitions=3
```

Do not add a full example-07 solve to ordinary CTest. The optimum is zero;
all terms are nonnegative and the returned zero-cost coloring is feasible.

Graph construction and a preflight assignment check run outside the measured
loop. Each measured iteration includes public API validation, preparation,
reconstruction, and returned-solution cleanup. Use these measurements to assess
scaling rather than asserting tight timing ratios in tests. Formatting includes
benchmark sources with the option off.

For memory-sensitive changes, an additional sanitizer build is useful:

```sh
cmake -G Ninja -S . -B build-clang-modules-sanitize -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=clang-21 -DCMAKE_CXX_COMPILER=clang++-21 \
  -DCMAKE_C_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer' \
  -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address,undefined'
cmake --build build-clang-modules-sanitize
ctest --test-dir build-clang-modules-sanitize --output-on-failure
```

LeakSanitizer cannot run under ptrace in some execution environments. In that
case, run the same tests with `ASAN_OPTIONS=detect_leaks=0` and report that
leak checking was unavailable. Documentation-only changes do not require
rebuilding or rerunning the test suite.

For ThreadSanitizer, instrument both PBQP and oneTBB. The script consumes an
existing oneTBB source checkout; it never downloads a scheduler automatically:

```sh
cmake -DPBQP_TBB_SOURCE=/path/to/oneTBB \
  -DPBQP_TSAN_ROOT=/tmp/pbqp-tsan -P cmake/RunTSan.cmake
```

It configures separate Debug builds with Clang 21, installs the instrumented
TBB under the chosen root, builds the actual module project, and runs parallel
and private-preparation tests with `TSAN_OPTIONS=halt_on_error=1`. The manual
`.github/workflows/tsan.yml` uses oneTBB v2022.3.0 and the same script.
An uninstrumented system TBB can produce scheduler synchronization reports even
for a minimal task-group example; do not suppress them to claim a clean run.
