# Implementation layout

The public interface is the C header `include/pbqp.h`. Internal C++ code belongs
to namespace `pbqp` and the named module `pbqp`; the primary module exports no
C++ implementation names. Physical directories describe responsibilities without
changing partition names or making private partitions available to clients.

| Directory | Responsibility |
| --- | --- |
| `src/` | Primary module and library assembly in CMake |
| `src/api/` | C entry points, validation boundaries, exception translation, diagnostics |
| `src/core/` | Structural validation, ownership, reference evaluation, dynamic bitsets, independent sets, internal failures |
| `src/io/` | Tokenization, parsing helpers, cost serialization |
| `src/search/` | Prefix jobs, dispatch, percentage boundaries, covered-space accumulation |
| `src/solvers/naive/` | Sequential and parallel exhaustive enumeration |
| `src/solvers/bnb/` | Unified B&B engine, reference-term bounds, optimization profiles, verified regalloc structure, capacity bounds, and pricing |
| `src/solvers/greedy/` | Cost-priority assignment |
| `src/solvers/reduction/` | Reduction graph, candidate worklist, propagation and reconstruction |

C entry points validate borrowed data, invoke the implementation, translate
exceptions, and transfer independent output ownership. Text I/O entry points
stay in `api/`; stream mechanics stay in `io/`. oneTBB headers and scheduler
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
