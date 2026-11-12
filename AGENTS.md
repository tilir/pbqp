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
  partitions (`module pbqp:name;`, stored as `.cppm`). Use module linkage
  instead of a `pbqp_detail` namespace or exported helper declarations.
  The primary `pbqp` interface exports no internal C++ names.
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
- `src/text_reader.cppm`: private `:text_reader` partition containing the stream
  tokenizer, comment handling, line tracking, and numeric parser.
- `src/problem.cppm`: private `:problem` partition containing structural
  validation and dimension checks.
- `src/problem_api.cpp`: C problem destruction and validation entry points.
- `src/io.cpp`: C text input/output entry points and error translation.
- `src/error.cppm`: private `:error` partition with non-throwing diagnostics.
- `src/failure.cppm`: private `:failure` partition for parsing/validation errors.
- `src/api_boundary.cppm`: shared C API exception translation, including
  allocation failures and container size limits.
- `src/ownership.cppm`: vector-backed problem and solution storage behind C
  views, with RAII ownership helpers.
- `src/evaluation.cppm`: shared reference evaluator for validated assignments.
- `src/progress.cppm`: exact percentage boundaries for mixed-radix enumeration
  without multiplying domain sizes into a fixed-width counter.
- `src/solution.cpp`: public assignment evaluator and solution destruction.
- `src/bruteforce.cpp`: validation and ownership boundary for exhaustive search.
- `src/bruteforce.cppm`: private `:bruteforce` partition with vector workspace
  and allocation-free lexicographic reference enumeration.
- `src/branch_and_bound.cpp`: validation and ownership boundary for exact search.
- `src/branch_and_bound.cppm`: private `:branch_and_bound` partition with cached
  unary, matrix, and row minima and iterative lexicographic depth-first search.
- `src/greedy.cpp`: validation and ownership boundary for the greedy C API.
- `src/greedy.cppm`: private `:greedy` partition with `GreedySearch`, separating
  workspace allocation, node priorities, edge indexing, option scoring, and
  assignment. Preparation allocates all search storage; `run` does not allocate,
  backtrack, or claim optimality.
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
Do not duplicate allocation checks in solver logic. Retain `std::unique_ptr`
for individual owned objects. Do not add `-fno-exceptions`; modules use
consistent compiler options. Search uses status returns; do not add exceptions,
allocation, or validation inside search loops.

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
`PBQP_INFEASIBLE`. Greedy returns FEASIBLE even when it happens to find an
optimum or solves the empty problem.

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

Branch-and-bound assigns nodes and options in ID order and retains the first
lexicographic optimum. Its lower bound substitutes each unresolved term with
an independent minimum, then adds all terms in reference evaluation order.
Use selected matrix entries for fully assigned edges, row minima for assigned
sources, and matrix minima otherwise. Since src < tgt, assigned prefixes need
no column minima. Rounded addition is monotone for these finite input terms;
this establishes a bound despite negative costs or infinite accumulated sums.
Do not regroup suffix sums or prune solely on the partial assigned cost.
An explicit INF term proves a prefix impossible, but accumulated infinity
alone does not. Run greedy during preparation to seed the upper bound using
its reference-order solution cost. A heuristic dead end supplies no bound;
continue exact search without treating it as infeasibility. Start with positive
infinity if no seed exists. Before the first feasible tuple found by exact
search, prune only when `L > G`: greedy's assignment may follow an earlier
equal optimum. After exact search finds a feasible tuple, prune `L >= G`, since
subsequent tied tuples cannot precede the incumbent. Lower G on each improvement.
This preserves lexicographic tie-breaking while closing equal-cost subtrees
without enumeration.
Cache minima and allocate all storage before iterative search. No recursion,
allocation, or exceptions are needed inside search. See
`doc/solvers.md` for the numerical argument and progress policy.

Input accepts decimal fractions and scientific notation. Keep the classic
locale for serialization and use `max_digits10` precision so finite float
costs round-trip. Preserve the PBQP 1 ordering and comment rules documented
in `doc/format.md`.

## Tools and integration

Use `add_subdirectory` and link the `pbqp` CMake target to embed the library.
C callers need the C++ runtime at final link time; CMake handles this.
The `pbqp_cli` target produces the `pbqp` executable. `pbqp_generator` is the
reusable generator library; `pbqp_generate` is its CLI. Both tools link the
shared `pbqp_command_line` library for argument parsing.

Invoke the solver as `pbqp --solve=naive INPUT`. `pbqp --help` lists available
solvers: `naive` selects exhaustive search, `bnb` exact branch-and-bound, and
`greedy` the heuristic. Do not
reintroduce `solve` or `dump` positional subcommands. Problem dumping remains
available to library users through `pbqp_write`.
`--heartbeat` is a valueless flag for `naive` and `bnb`. It writes one flushed dot to
stderr for each completed integer percentage, from 1 through 100, followed by
a newline at completion. Count all tuples, including forbidden assignments.
The first dot is emitted after ceil(total / 100) visited tuples; no zero-percent
notification is sent. For branch-and-bound, count all tuples in fully searched
or pruned subtrees, so a single cut may cross many percentage boundaries.
Small search spaces may cross several percentages on one tuple. Progress
counts covered space, not elapsed time or an ETA.

The library exposes `pbqp_solve_bruteforce_with_progress` with an optional
synchronous C callback, and the corresponding
`pbqp_solve_branch_and_bound_with_progress` entry point. Keep terminal output
in the CLI. Callbacks must not
throw or mutate the input. Progress scratch storage is allocated before
enumeration. The no-progress path uses a compiled-away observer, with no
per-tuple progress branch. Preserve exact boundaries even if the domain-size
product exceeds size_t or uint64_t; do not saturate the total or report fake
early percentages. The mixed-radix boundary is ceil(total * p / 100) - 1.
`PercentProgress::completed_prefix` compares boundaries against the final
tuple of a closed prefix, implicitly treating its unassigned suffix as maximal.

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
It uses Ubuntu 24.04, the official LLVM apt repository, Clang 21, and Ninja.
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
assignments/costs, and a retained failing problem dump. Test checks use
`std::source_location` so ordinary failures identify their source line.
Also preserve direct C API validation, ownership lifetime checks, partial-read
cleanup tests, I/O failures (including `/dev/full` when available), greedy
traps, and sparse graph generation without quadratic pair scans.
Heartbeat tests cover the C callback, unchanged solver output, the complete
1..100 notification sequence, empty and infeasible searches, and no early
progress on a timed-out 2^65-tuple search. Do not run `05_too_long_for_naive.pbqp`'s complete
2,176,782,336-tuple enumeration as a routine CTest entry.
Preserve greedy-seed regressions for dead ends, suboptimal initial solutions,
lexicographic ties, reference rounding, and infinite accumulated seed costs.
Compare branch-and-bound with both naive and the independent oracle on all
small generator families, including fractional spill costs and extreme float
values. Preserve targeted negative-edge and cancellation regressions; a
reassociated or partial-only bound must not discard a better solution.
Check 100 callbacks on huge feasible and root-infeasible 2^65-tuple spaces
that branch-and-bound can close quickly.

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
