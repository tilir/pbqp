# Implementation layout

The public interface is the C header `include/pbqp.h`. Internal C++ code belongs
to namespace `pbqp` and the named module `pbqp`; the primary module exports no
C++ implementation names. Physical directories describe responsibilities without
changing partition names or making private partitions available to clients.

| Directory | Responsibility |
| --- | --- |
| `src/` | Primary module and library assembly in CMake |
| `src/api/` | C entry points, validation boundaries, exception translation, diagnostics |
| `src/core/` | Structural validation, ownership, reference evaluation, dynamic bitsets, independent sets, cooperative deadlines, internal failures |
| `src/io/` | Tokenization, parsing helpers, cost serialization |
| `src/search/` | Prefix jobs, dispatch, percentage boundaries, covered-space accumulation |
| `src/solvers/naive/` | Sequential and parallel exhaustive enumeration |
| `src/solvers/bnb/` | Unified B&B engine, reference-term bounds, optimization profiles, verified regalloc structure, capacity bounds, and pricing |
| `src/solvers/bucket/` | Induced-graph planning, dense min-plus kernels and argmin reconstruction |
| `src/solvers/dsatur/` | Verified-model DSATUR/tabu coloring and spill repair |
| `src/solvers/greedy/` | Cost-priority assignment |
| `src/solvers/reduction/` | Reduction graph, candidate worklist, propagation and reconstruction |

C entry points reset outputs and delegate to named internal functions through
`api_call`, which clears errors, forwards arguments, and translates exceptions.
Private `*_api.cppm` partitions own validation, options, preparation, and output
transfer; public `.cpp` units keep the C definitions thin. Validation's void
internal function can be passed directly, with normal return meaning OK. Avoid
wrapping whole API implementations in capturing lambdas. Text I/O entry points
stay in `api/`; stream mechanics stay in `io/`. `problem_reader.cppm` handles
both text versions, retains compact sparse definitions, and materializes one
dense array per referenced PBQP 2 matrix;
`problem_writer.cppm` interns matrix views and selects dense/sparse encoding.
The C boundary keeps file opening, validation, and exception translation. oneTBB headers and scheduler
calls stay in `search/parallel.cpp` because some TBB helpers cannot be serialized
in a GCC module partition.

`core` and shared `search` must not import concrete solvers. The B&B orchestrator
may invoke greedy and reduction for incumbent seeds; its bounds and workers do
not depend on either heuristic. Shared mechanisms include prefix descriptors,
dispatch, exact naive progress, and reference evaluation. B&B owns approximate
surviving-tree progress, including cached child lookahead and donated subtree
weights. Baseline and optimized B&B share identical worker/control semantics
through a bounds-strategy template, with original-ID assignments and ordered
search positions. The single C B&B boundary instantiates the same strategy for
all profiles: its generic relaxation delegates to the reference bounds, while
eligible enabled structural terms strengthen it. CLI names only choose the
default profile. `bnb_options.cppm` owns the shared profile definitions;
`api/solve_options.cppm` validates the open key-value map and `api/bnb_api.cppm`
resolves profile/boolean settings before search preparation. Winner selection stays
specific to the algorithm: naive preserves the first lexicographic minimum,
while B&B shares an any-optimum incumbent.

`BitSet<Word = unsigned>` owns reusable word-vector storage. Its const forward
range yields ascending indices of set bits without allocation. `num_bits()`
reports the domain extent and `count()` reports population; the range is unsized
because population is not cached in constant time. Its unsigned word
type is independent of bit count. Generic `IndependentSets` accepts adjacency
bitsets and enumerates maximal sets directly; verified register structure and
interpreting sets as register classes belong to the B&B structural path. A bounded all-subset reference lives only in private tests
for differential checks and historical budget regressions. Pricing is also
solver-specific and supplies integer constraints to the reference-order cost DP.
`bnb_pricing.cppm` prepares shared root weights; `bnb_prefix_pricing.cppm`
uses preallocated worker-local scratch for conditioned refinements. The complete
catalogue also feeds the bounded primal seed in `bnb_cover.cppm`, invoked by
orchestration rather than by bounds or workers.

## Unified B&B implementation

`src/solvers/bnb` owns the entire B&B implementation for O0/O1/O2.
`bnb_bounds.cppm` defines `BranchAndBoundBounds`, composing the reference-term
relaxation in `bnb_term_bounds.cppm` (`BranchAndBoundTermBounds`) with enabled
structural bounds. Term minima, model checks, and generic matrix-based forward
checking remain a cohesive helper rather than being mixed into capacity DPs.

`branch_and_bound.cppm` orchestrates root checks, heuristic seeding, planning,
workers, and result ownership. Its planner, queue, worker, and incumbent work
with a supplied bounds strategy; private tests can exercise the term relaxation
independently. The public B&B entry always uses the common composition and
resolves its profile before preparation. `bnb_structure.cppm`,
`bnb_pricing.cppm`, `bnb_prefix_pricing.cppm`, and `bnb_cover.cppm` provide optional
structural components in the same directory, with reports in
`bnb_diagnostics.cppm`. `bnbopt` is a CLI name selecting default profile O2,
not a separate implementation subtree or C entry.

Generic prefix descriptors and TBB services remain in `src/search`, together
with naive's exact progress counters. B&B's approximate progress reporter lives
in `bnb_progress.cppm`.

B&B's work queue owns prepared prefix slots and selects cached lower bounds.
Workers donate sibling ranges only between DFS chunks and remove those ranges
from their private limits. Queue cancellation wakes blocked consumers before
TBB joins on scheduler failure. Shared naive planning remains separate so its
lexicographic minimum and compiled-away sequential progress path are preserved.

Every subsystem lists its exact sources in its local `CMakeLists.txt`, registers
those same paths for formatting, and propagates the aggregate lists upward.
`src/CMakeLists.txt` attaches the collected partitions to one PRIVATE module file
set on the `pbqp` target. Modules are compiled once, with no generated mirrors or
exported implementation helpers. Test and benchmark sources are registered for
formatting even when those targets are disabled.

`tests/search_test_support.cpp` and `tests/bnb_bounds_test_support.cpp` are test-only
implementation units of `pbqp`, compiled in the owning library target when
`BUILD_TESTING` is enabled. They import private partitions and check preparation
invariants, prefix jobs, structural eligibility, and numerical lower bounds. A namespaced C++ linkage
bridge lets the ordinary test executable invoke these checks without exposing
internal types or extending the C API. Production builds with testing disabled
do not include the bridge.

See [verification.md](verification.md) for coverage and measurement commands,
[AGENTS.md](../AGENTS.md) for development rules and [solvers.md](solvers.md)
for algorithm, numerical, and progress contracts.


## Generator implementation

`tools/generator.cppm` is the public `pbqp.generator` interface: options, owning
problem views, and short accessors. Long sampling and generation routines live
in `generator.cpp`; its non-exported `:sampling` partition implements the fixed
integer and binary32 conversions over standard mt19937_64. Keeping that import
in the implementation lets CMake keep the partition PRIVATE while the primary
interface remains PUBLIC. Test-only `generator_test_support.cpp` compiles inside
the owning generator module and checks golden conversion vectors.

Regalloc generation owns one shared interference matrix, and PBQP 2 parsing
retains shared matrix arrays behind C views. `cmake/GeneratorVersion.cmake`
configures a private identity header from an explicit source list; formatting
registers its template, not generated output. CLI provenance is appended as
comments after END without extending the C problem struct or retaining comments
inside library views.

## Cooperative time budgets

`core/deadline.cppm` holds a monotonic start time and a millisecond budget; zero
is unlimited. Copies share the logical deadline without mutable shared state.
Exact API boundaries create it before validation/preparation and pass it through
orchestration, optional preparation, and workers. It creates no timer thread.
Heuristic seed helpers may consume it internally; standalone heuristic entries
do not consume `timeout-ms`.

Naive specializes timed/untimed loops, polling every 256 tuples only in timed
calls. Each parallel consumer stops against the same deadline and joins before
the API translates interruption to `PBQP_TIMEOUT`. B&B polls between short DFS
chunks (32 bound checks with a timeout, normally 512) and cancels its queue,
waking blocked consumers. It also checks preparation boundaries and bounded
clique/catalogue/pricing/seed iterations. A partial plan or catalogue is never
used as a completeness certificate. Expiration discards partial minima and
incumbents under the normal failure/output-NULL ownership contract. Only
finished proofs finalize progress. Individual allocations, validation passes,
bounds, reductions, callbacks, scheduling, and cleanup remain non-preemptible.

Generator provenance is published with the task by a same-directory staged
rename. Its source identity reuses the component-owned production library and
tool source aggregates, with normalized LF contents and paths relative to the
checkout. Including the whole production library is conservative but prevents
changes in the C I/O boundary or new partitions from escaping the identity.

## Coloring heuristic

`core/interference.cppm` owns the original-input model check and its cached
failure metadata. Term bounds, diagnostics, and DSATUR share that check without
making `core` depend on a solver. B&B passes its cached decision to the seed;
coalescing and nonuniform domains never enter specialized recoloring.

`solvers/dsatur/dsatur.cppm` owns CSR adjacency, color counts, tabu tenure,
construction, conflict-reducing local moves, spill repair, resource accounting,
and independent solution storage. Preparation allocates every buffer; run does
not allocate, log, or throw. `dsatur_diagnostics.cppm` formats cached eligibility,
limits, counters, and outcomes outside local-search loops. `api/dsatur.cpp`
composes this heuristic with a greedy fallback and keeps the better valid result.
B&B orchestration invokes DSATUR independently after greedy and before reduction
when `dsatur-seed` is enabled; the seed needs neither structural bound workspace
nor a complete independent-set catalogue. Bounds and workers do not import it.
