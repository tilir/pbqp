# Example 08: a gap in the current B&B formulation

The file [08_too_long_for_simple_bnb.pbqp](../examples/08_too_long_for_simple_bnb.pbqp)
is difficult for the current ID-ordered B&B, but is readily solved by other
exact formulations. The experiments below change no production solver code.

## Structure

- 40 nodes, 650 edges out of 780 possible pairs; edge density is 5/6.
- Every node has 13 options: a positive-cost spill (option 0) and 12 free,
  interchangeable registers.
- Every matrix has INF on the register diagonal and zero everywhere else,
  including the spill row and column. These properties were checked for every
  input entry.
- The interference graph has maximum clique size 15. One witness is
  `{3, 6, 8, 9, 11, 14, 16, 22, 23, 27, 28, 30, 32, 35, 39}`.
- There are exactly 209 nonempty independent sets: 40 singletons, 130 pairs,
  37 triples, and two quadruples. They can be enumerated as cliques of the
  sparse complement, which has 130 edges.

At least three vertices of the clique must spill. Its three cheapest vertices
already give a real-arithmetic lower bound of approximately 63.0123. The current
B&B root bound is zero: every unary term can independently choose a register,
and every edge minimum is zero. These independent choices do not describe a
simultaneously feasible coloring. Forward checking sees the register shortage
only after enough neighboring nodes have been assigned.

Register-renaming symmetry is already detected by B&B for this input. The gap
is therefore not an omitted permutation-symmetry check.

## Native B&B experiment

Clang Release CLI runs, without heartbeat, gave the following observations.
Timeouts are lower bounds on completion time, not returned partial solutions.

| Input order | Threads | Wall time |
| --- | ---: | ---: |
| Original IDs | 1 | Did not finish within 15 s |
| Original IDs | 16 | Did not finish within 15 s |
| Clique witness first, remaining nodes by descending degree | 1 | About 3.5 s |
| Same clique-first order | 16 | About 0.38 s |

Sorting all nodes by degree alone still timed out after 10 seconds. The
16-thread original run consumed about 224 CPU-seconds in 15 wall-seconds, so
parallel consumers were active.

The clique-first experiment used a temporary relabeled copy. Greedy's seed was
206.713089 for both orders. Reduction's seed worsened from 208.031235 to
294.303223 after relabeling; greedy therefore supplied the same upper bound.
The speedup did not depend on receiving a better initial heuristic solution.
Both completed runs returned the same optimal spill set after mapping IDs back.

Relabeling also changes unary reference summation order. The mapped assignments
were separately evaluated on the original input through the actual library's
`pbqp_evaluate_assignment`, obtaining feasible cost 161.241684. A production
ordering improvement must change traversal order while retaining original unary
and edge summation order, and must handle edges whose target is assigned first.

## Independent exact model

Each register can hold one independent set. Introduce a binary variable `x_S`
for each of the 209 nonempty independent sets, and `y_v` for each spilled node:

```text
minimize  sum_v spill_cost[v] * y_v
subject to
          sum_{S containing v} x_S + y_v = 1    for every node v
          sum_S x_S <= 12
          x_S, y_v binary
```

This is an exact representation of feasible assignments up to register names:
selected independent sets are disjoint color classes, and unused registers need
no variables. It has 249 binary variables and 41 constraints.

The system GLPK library solved its LP relaxation at 155.310411453247 and proved
an integer optimum of 161.241683959961 in about 0.7 ms, excluding input parsing
and independent-set enumeration. This is a diagnostic solve, not a new project
dependency or a controlled Google Benchmark result.

An optimal witness spills nodes `{5, 15, 16, 22, 29, 32, 39}` and uses these
12 register classes:

```text
{0, 1, 9}      {2, 14, 31}   {3, 37}       {4, 21, 35}
{6, 25}        {7, 23, 34}   {8, 36}       {10, 17, 18, 27}
{11, 13, 20}   {12, 19, 30}  {24, 26, 33}  {28, 38}
```

Original greedy and reduction solutions spill nine nodes and cost 206.713089
and 208.031235 respectively. The optimum spills seven nodes.

The model uses the parsed float costs as exact binary-valued coefficients in
double precision, whereas PBQP evaluates in sequential float order. Excluding
the optimal spill set and solving again gives the next spill-set objective
165.343485832214, a gap of about 4.1018. All spill prices are positive and their
sum is below 2044. Even the conservative rounding-error bound of 0.005 for any
40-term float sum is far below this gap. Matrix terms are zero for every
feasible assignment. Thus another spill set cannot win through reference-order
rounding; colorings with the same spill set have identical unary terms.

## Implications

The instance exposes weak global bounds and an unfavorable fixed search order.
A clique-first or more-constrained-first traversal deserves implementation and
regression coverage while preserving reference-order evaluation. Clique
capacity constraints can strengthen pruning. For strictly recognized regalloc
inputs, searching over independent register classes is another promising exact
strategy; the small pattern count is particularly favorable on this fixture.
These conclusions do not imply that arbitrary PBQP or every dense regalloc
instance has a similarly small exact formulation.

## Implemented optimized solver

`bnbopt` now implements structural clique-first traversal, bit-mask forward
checking, and reference-order DP bounds for clique spill-count constraints.
`bnb` retains ID-ordered baseline bounds. Both use the same original-order
reported objective and share exact-search mechanics. See [solvers.md](solvers.md)
for the eligibility checks, bounded preparation, and numerical argument.

On 2026-11-14, Clang Release Google Benchmark medians over three repetitions
measured the implemented solver on the original, unpermuted inputs:

| Example | Threads | Heartbeat | Median solve time |
| --- | ---: | --- | ---: |
| 06 | 1 | Off | 0.483 ms |
| 07 | 1 | Off | 1.935 ms |
| 08 | 1 | Off | 71.5 ms |
| 08 | 4 | Off | 22.9 ms |
| 08 | 16 | Off | 15.2 ms |
| 08 | 16 | On | 16.2 ms |

Every preflight checked OPTIMAL quality, feasibility, and the known original
reference-order cost. Parsing is outside timing; API validation, preparation,
search, and solution cleanup are included. Small instances may be slower with
more threads because preparation and scheduling become a larger share of work.
These are observations for this build and machine, not CTest timing thresholds.

```sh
./build-clang-modules/pbqp_benchmark_bnb \
  --benchmark_filter=BranchAndBoundOpt --benchmark_repetitions=3
```
