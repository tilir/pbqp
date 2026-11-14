# Verification and performance measurements

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
The Release job checks formatting and uses a shared library to exercise C, C++,
Python, and Ruby programming examples and binding tests. Release CI selects
Ruby 3.4 with `ruby/setup-ruby`, checks Fiddle availability, and logs the runtime
version instead of relying on Ubuntu's default Ruby. The sanitizer job uses
a static library and exercises native examples. Both jobs build and
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
`07_parallel_exact_beats_heuristics.pbqp` runs only through greedy and r1r2rn in tests;
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
  --benchmark_out=build-clang-modules/pbqp-reduction.json --benchmark_out_format=json
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

`pbqp_benchmark_bnb` loads examples 06 and 07 from the checkout root. Example 06
compares 1/2/4/8 requested threads; example 07 compares 1/4/16 with heartbeat
enabled and disabled, covering progress contention. Both use `UseRealTime`, so
worker CPU time is not mistaken for elapsed solve time. Parsing and an optimum
and assignment check are untimed. Optimized cases `BranchAndBoundOpt/Example06`, `/Example07`, and `/Example08`
compare baseline/optimized behavior on the original, unpermuted fixtures.
Example 09 adds 1/4/16-thread and heartbeat cases with known optimum
338.854034; do not run baseline exact solvers on it in routine tests.
Example 10 adds 1/4/16-thread cases, with heartbeat variants on 1 and 16 threads,
and known reference-order optimum 369.419556. It covers catalogue seeding and
prefix-conditioned pricing; do not run baseline exact solvers on it in CTest.
Example 08's known optimum is 161.241684; it is a benchmark, not a full baseline
exact-search CTest. The benchmark heartbeat uses a no-op callback
to measure progress accounting without terminal I/O. Run:

```sh
cmake --build build-clang-modules --target pbqp_benchmark_bnb
./build-clang-modules/pbqp_benchmark_bnb --benchmark_repetitions=3 \
  --benchmark_out=build-clang-modules/pbqp-bnb.json --benchmark_out_format=json
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

Do not add a full example-06 solve to ordinary CTest. The optimum is zero;
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
  -DPBQP_TSAN_ROOT=build-clang-modules-tsan -P cmake/RunTSan.cmake
```

It configures separate Debug builds with Clang 21, installs the instrumented
TBB under the chosen root, builds the actual module project, and runs parallel
and private-preparation tests with `TSAN_OPTIONS=halt_on_error=1`. The manual
`.github/workflows/tsan.yml` uses oneTBB v2022.3.0 and the same script.
An uninstrumented system TBB can produce scheduler synchronization reports even
for a minimal task-group example; do not suppress them to claim a clean run.

## Public wrappers and bindings

The C test compiles as C. `cpp_api` checks RAII, borrowed spans, move-only
ownership, error metadata, and callback exception containment. Shared builds
with `PBQP_BUILD_EXAMPLES=ON` also exercise Python/Ruby clients and bindings
when their interpreters are present; see the [programmer's guide](programmers_guide.md).
Ruby ownership regressions use mock deleters for dup/clone/freeze, alias closure,
and GC to avoid making a double-free regression crash the test runner.
Create abandoned aliases in a separate, completed fiber: MRI's conservative
native-stack scan can otherwise retain stale temporary pointers after a helper
returns, making an immediate weak-reference assertion depend on the interpreter
build. Keep the collection and exactly-once deallocation checks intact.

The private optimized-bound checks retain all original-order completion oracles.
The production maximal catalogue and test-only all-subset reference are compared
with exhaustive subset predicates.
Per-register capacities are compared for all fixed independent classes and
pending allowed masks on small graphs, using several nonnegative weight vectors.
Include isolated vertices, disconnected components, unequal maximal-set sizes,
and multiword sets. Complete bipartite K(6,6), K(12,12), K(13,13), and K(16,16)
check catalogue size and exact root bounds. Nine disjoint triangles exercise the
10,000 maximal-set limit; large isolated graphs cover word/storage budgets.
Quantization tests cover half-unit boundaries, zero/nonfinite/negative prices,
scaled weights near the state limit, and sum overflow. An incomplete catalogue
must be empty, with pricing disabled and other valid
bounds retained. No test depends on elapsed-time speedups.

Build the optional `docs` target to validate Doxygen public-header comments with
warnings treated as errors. Documentation-only changes need no native rebuild.

Preliminary-bound diagnostics cover the minimum remaining frontier, capping by
an improved incumbent, closed optimal/infeasible roots, absence of an incumbent,
and feasible infinite costs. CLI coverage checks that the interval precedes
heartbeat output and leaves solution stdout unchanged.

B&B heartbeat regressions use synthetic wide domains with 98% immediate
infeasibility or bound cuts. Check that these cuts do not advance progress,
child bounds are evaluated once, small DFS quanta can pause child lookahead,
and actual sibling donation conserves completed plus discarded weight.
Completion is reserved until the search joins, including empty incoming jobs.
Retain naive's independent exact mixed-radix threshold tests. Example 10 is a
manual long-running heartbeat check; do not exhaust it in ordinary CTest or
assert timing thresholds for its dots.

Prefix-pricing regressions check every canonical prefix against independent
original-order completions, reuse weights after backtracking and at the root,
and exercise the 66th register on a 70-node graph. A complete 1025-pair catalogue
exceeds the register-pattern candidate budget while retaining an independently
certified root capacity. Seed tests cover fractional, signed, explicit-INF,
and reachable overflowed costs without changing input arrays.


## Optimization profiles and the open option map

The public recursive oracle compares O0/O1/O2, each individual positive/negative
optimization override, and mixed switch sets on small generated problems.
Private conditioned-bound tests also disable symmetry, forward checking,
structural order, and clique bounds together, checking every noncanonical prefix
against independent original-order completions. A sparse high-register prefix
covers arbitrary register IDs when symmetry is off.

C checks cover tagged map layout/linkage, defaults, empty maps, missing entries,
invalid keys/tags, wrong consumed types, invalid profiles/booleans, unknown keys,
last-entry precedence, and independent callback contexts. CLI cases cover both
names under all profiles, equivalent O0 output, positive and negative overrides,
last-flag/profile precedence, dependencies, and unsupported options. C++/Python/
Ruby wrappers exercise open scalar maps and reject invalid profiles/types while
retaining ownership and callback behavior.


Unknown-option regressions cover warning callbacks and independent contexts,
diagnostic fallback, warning delivery without verbose CLI output, last-key
precedence, per-solver recognition, and strict failures with no callbacks/search.
Both CLI tools exercise `-Werror` in either argument order and unchanged outputs.
Bindings cover native strict mode and language warning emission after returning
from C, including cleanup when Python warning filters raise.

Bitset range checks use independent boolean-vector indices across all supported
word widths, empty/full/random sets, padding, and empty intervening words. Check
const/common/forward iterator concepts, unsized range semantics, postfix
increment, multipass copies, and standard range/iterator algorithms.


## PBQP 2 and reproducible generation

Round-trip tests cover PBQP 1 compatibility, PBQP 2 dense/sparse definitions,
shared matrix ownership, rectangular dimensions, INF/nonzero defaults, signed
zero, original edge order, and unused definitions. Reject malformed IDs,
references, dimensions, sparse coordinates/counts, duplicate cells, costs, and
all truncated token prefixes; no failed read may return a partial object.
Examples 06 through 10 retain bit-identical costs and edge order after conversion.

Generator tests pin integer rejection/full-width/zero-width draw consumption,
subnormal/extreme/equal-bound float samples, and numeric fingerprints for all
three families under direct and complement edge sampling. Run the same tests
with libstdc++ and libc++; the Release libc++ CI configuration uses
`-DCMAKE_CXX_FLAGS=-stdlib=libc++`. Both use the standard mt19937_64 engine.
CLI tests compare task data independently of provenance, query --version, and
replay a recorded command with spaces, quotes, and a newline in the output path.
