# Optimizations in bnbopt

`bnbopt` is an exact solver built on the same search engine as `bnb`. It retains
the C API contracts, any-optimum tie policy, thread limit, and covered-space
heartbeat. Both solvers report costs using unary terms in original node-ID
order, followed by edge terms in stored order. Neither proves optimality for
a regrouped real-arithmetic objective.

## Baseline and shared search machinery

`bnb` uses ID-order assignments, unary-cost option ordering, prefix forward
checking, and independent unary/row/matrix minima. Greedy and R1/R2/RN provide
initial feasible incumbents. Verified pure-interference inputs also allow
canonical register naming; this is already part of the baseline.

Both solvers share budgeted prefix planning, cached-bound priorities, private
resumable DFS, a prepared coarse-work queue, and synchronous oneTBB consumers.
The frontier target is `max(32, 2 * available_workers)`, with at most 512 bound
calls or 25 ms of planning. A running bound can exceed the time budget.
Unchecked siblings become range jobs. Forced runs are compressed and checked
once. Between chunks of 512 bound checks, a worker can donate unexplored
siblings to a waiting consumer and remove those ranks from its private limits.
This schedules coarse subtrees, not individual DFS vertices.

These scheduling improvements apply to both `bnb` and `bnbopt`; they are not
an additional bound supplied only by `bnbopt`. Serial warming was evaluated
but is not enabled. TBB and application-wide concurrency limits may reduce the
prepared worker count. On scheduler failure, queue cancellation wakes waiting
consumers before joining.

## 1. Recognize the pure-interference model

The additional graph-based optimizations require every original input term
to match this model:

- All nodes have the same domain: spill option 0 and at least one register.
- Register unary costs are zero. Spill costs may be finite or positive infinity.
- Every stored edge has INF on equal-register pairs and zero elsewhere,
  including spill rows and columns.

This is a subset of register-allocation PBQP. Coalescing costs can violate it
already in the original input. R2 fill matrices need not preserve it either.
Failing this sufficient shape test does not imply absence of register-label
symmetry. Unsupported input uses the ordinary PBQP bounds and remains a valid
problem. The reduction seed operates on an owned graph copy and never replaces
the original input used by exact search.

Node and register sets use `BitSet<Word = unsigned>`. Their size is dynamic;
there is no fixed 64-node or 64-register limit. Dense graph and clique-search
storage has a 64 MiB preparation budget. Exceeding it disables the structural
path and retains the generic bounds.

## 2. Branch in structural order

The solver builds the interference graph, collects greedy cliques, and refines
a large clique with iterative bounded search. A selected clique anchors the
branching order. Remaining nodes prefer connections to the chosen prefix,
then degree, spill cost, and ID. A large ordering workload uses a static tail
instead of repeated adaptive rescoring.

Preparation limits are 64 greedy starts, 100,000 refinement extensions, a
64-million-word ranking budget, and 32 retained clique witnesses. Stopping
clique discovery early only weakens the bound: every retained clique is valid,
and a maximum clique is unnecessary for correctness. These are preparation
limits, not input-size limits.

Assignments remain indexed by original node ID. Prefix ranks and DFS cursors
use search positions. Ordered domain views serve planning and progress only;
they do not change reference cost evaluation. Canonical register names follow
first appearance in this search order.

## 3. Enforce clique register capacity

Forward checking records allowed registers against the fixed prefix. For a
clique's unassigned nodes `U`, let `A_v` be each node's allowed register set.
At most `|union A_v|` nodes can remain in registers, so at least

```text
max(0, |U| - |union A_v|)
```

must spill. A capped spill-count DP enforces this necessary condition. It
processes every unary term in original node order, including assigned terms
and minima outside the clique. Register transitions add zero; spill transitions
add the original unary spill cost with `pbqp_cost_add`. It retains the cheapest
reachable prefix for each state.

Reachability is separate from cost: an allowed tuple whose sum overflows to
infinity must not be confused with an explicit INF prohibition. Different
clique relaxations are combined by maximum, not by adding overlapping costs.

## 4. Enforce global register capacity

A register can hold one independent set of the interference graph. The generic
`IndependentSets` enumerator lives in `core`; it accepts an undirected adjacency
graph without depending on PBQP or register allocation.

Preparation is iterative, with a 16 MiB auxiliary-storage budget, a 10,000-set
limit, and an eight-million-word work budget. A partial enumeration gives a
lower bound on maximum capacity and is unsafe for pruning. On exhaustion,
partial results are discarded and this optimization is disabled.

For each register, find the largest allowed extension of its assigned class
into the unassigned nodes. Ignore competition with other registers and sum
those individual capacities. This overestimates how many nodes can stay in
registers; the positive remainder must spill. The original-order spill-count
DP enforces that global requirement.

## 5. Use auxiliary integer-weight capacity

Cardinality treats every unassigned node equally. For finite nonnegative spill
costs, a bounded projected accelerated-gradient heuristic can instead choose
nonnegative auxiliary prices. Double arithmetic here chooses weights only;
it never supplies a pruning cost.

Prices are quantized into integer weights. For each register, the actual maximum
remaining allowed weight is recomputed over the complete independent sets.
If the sum cannot accommodate all unassigned weight, the remainder must spill.
A capped-weight DP enforces this condition while still adding original float
costs, in original order. Any nonnegative weights give a valid capacity
constraint; improving their choice affects strength, not correctness.

Pricing uses at most 2,000 iterations at four temperatures and an eight-million
pattern-vertex visit estimate. Nonpositive heuristic scores, unsupported spill
costs, and exhausted preparation budgets retain the unweighted bounds. Integer
conversion and sums are checked before allocation. Scaling limits total weight
to 32,768 and the worst-case weighted DP scan to approximately four million
state slots. Negative or INF spill costs remain supported by the generic,
clique, and unweighted paths.

Cheap root bounds, heuristic seeds, and structural proofs precede capacity
preparation. No external LP or mathematical-programming library is required.

## Diagnostics and verification

`pbqp --solve=bnbopt --verbose INPUT` reports which paths are available and
used, together with reasons for skipped or limited preparation. Completing a
cheap proof can make later optimizations unnecessary. Exhausting a clique
search budget preserves valid witnesses; exhausting independent-set enumeration
instead disables capacity bounds. These decisions must remain distinct. Verbose
output includes the actual number of retained cliques, examined sets, pricing
iterations, effective integer-weight
limit and scaling, planned jobs, prepared workers, and any planning cutoff.
Library callers set `diagnostics` and `diagnostic_data` in `pbqp_solve_options`
passed to `pbqp_solve_branch_and_bound_optimized`;
CLI formatting stays in the tool and diagnostic text never replaces stdout
solution records.

Private tests compare generic independent sets against subset enumeration and
bounds against independent exhaustive completion of canonical prefixes. They
cover signed and fractional costs, explicit INF, cancellation, reachable
infinite accumulated costs, auxiliary-weight scaling, and fallback behavior.
Parallel tests cover actual sibling donation, exact serialized progress, and
queue cancellation on dispatcher failure. Public tests retain independent
assignment evaluation, input ownership, C compatibility, and heuristic quality.

## Reproduce performance measurements

The optional Google Benchmark target uses actual public API calls. Parsing and
preflight optimum/assignment validation are untimed; validation, preparation,
search, and solution cleanup are timed. Cases use `UseRealTime` and include
thread counts and no-op heartbeat callbacks.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPBQP_BUILD_BENCHMARKS=ON
cmake --build build --target pbqp_benchmark_bnb
./build/pbqp_benchmark_bnb --benchmark_filter=BranchAndBoundOpt/Example09/ \
  --benchmark_repetitions=3
```

[09_more_bnbopts.pbqp](../examples/pbqp/09_more_bnbopts.pbqp) has 60 nodes, 1,500
edges, and 16 registers. Its graph has 441 nonempty independent sets and none
larger than three, so at least 12 nodes must spill. Its weighted optimum instead
spills 13 nodes and has reference cost `338.854034`.

The maximum clique has size 19. Its initial clique relaxation is much weaker
than global weighted capacity, making this example useful for evaluating the
additional bounds. Measurements depend on preparation, incumbent discovery,
frontier shape, and scheduling; use the benchmark rather than asserting timing
ratios in correctness tests. The other exact-solver benchmark cases remain
useful for detecting overhead on easier inputs.
