# Branch-and-bound optimizations

`bnb` and `bnbopt` select default optimization profiles of one exact solver. It retains
the C API contracts, any-optimum tie policy, thread limit, and approximate
search-tree heartbeat. Both solvers report costs using unary terms in original node-ID
order, followed by edge terms in stored order. Neither proves optimality for
a regrouped real-arithmetic objective.

## Optimization profiles and overrides

Both CLI names use the same engine and C entry. `bnb` defaults to `-O1`;
`bnbopt` defaults to `-O2`. Select a profile explicitly through either name:

| Profile | Defaults |
| --- | --- |
| `-O0` | Basic B&B only: independent unary/pairwise term minima in reference order, node/option ID order, mandatory feasibility checks, and incumbent pruning. No heuristic seeds, symmetry cuts, forward checking, frontier planning, donation, or structural preparation. |
| `-O1` | O0 plus option ordering, forward checking, verified register symmetry, greedy/reduction seeds, prefix planning, and work donation. This is the former baseline `bnb`. |
| `-O2` | O1 plus structural order, clique/cardinality/weighted bounds, prefix repricing, and the independent-set coverage seed. This is the former `bnbopt`; new structural optimizations join this profile by default. |

Every name in the table below supports both `-fNAME` and `-fno-NAME`. The last
`-O` chooses the base profile; the last explicit form for each optimization
overrides that base, independent of where the profile occurs in the command.
Repeated named solver CLI options also use their last value. The generator's
separate duplicate-rejection policy is unchanged. Unknown option tokens warn
and are skipped; `-Werror` promotes these warnings to errors. Known options with
invalid values still fail.

| Optimization name | Default in | Effect when enabled |
| --- | --- | --- |
| `option-order` | O1/O2 | Visit each node's options by unary cost, then ID. |
| `forward-checking` | O1/O2 | Filter unassigned domains against fixed neighbors. Mandatory fixed conflicts are checked even when disabled. |
| `register-symmetry` | O1/O2 | Canonicalize interchangeable register names after verifying the input model. |
| `greedy-seed` | O1/O2 | Try greedy before branching. |
| `reduction-seed` | O1/O2 | Try R1/R2/RN if an incumbent improvement can still matter. |
| `prefix-planning` | O1/O2 | Prepare a budgeted frontier rather than one root job. |
| `work-donation` | O1/O2 | Donate unexplored sibling ranges to waiting workers between DFS chunks. |
| `structural-order` | O2 | Branch in clique-first/adaptive structural order rather than node ID order. |
| `clique-bounds` | O2 | Strengthen the lower bound with clique spill-count DPs. |
| `cardinality-bounds` | O2 | Use global unweighted capacity from complete maximal independent sets. |
| `weighted-bounds` | O2 | Prepare root prices and use auxiliary integer-weight capacities. |
| `repricing` | O2 | Refine those weights for selected fixed prefixes. Requires `weighted-bounds`. |
| `cover-seed` | O2 | Seed an incumbent by bounded independent-set coverage. |

```sh
# Equivalent baseline defaults through either CLI name.
pbqp --solve=bnbopt -O1 INPUT
pbqp --solve=bnb -O1 INPUT
# Add individual structural bounds to the baseline.
pbqp --solve=bnb -O1 -fweighted-bounds -frepricing INPUT
# Keep O2 except prefix repricing.
pbqp --solve=bnbopt -fno-repricing INPUT
# Start at O0, but distribute the search across coarse initial jobs.
pbqp --solve=bnb -O0 -fprefix-planning --numthreads=4 INPUT
```

O0 remains branch-and-bound, not naive tuple enumeration: safe reference-term
bounds, feasibility checks, and incumbent pruning define the algorithm. They
remain active in every profile. O0 disables both ways of distributing subtrees;
requesting multiple threads alone cannot create parallel work. Re-enable
planning or donation to make those threads useful.

Disabled components skip their preparation when no other enabled consumer needs
it. Clique discovery serves both clique bounds and structural order. The
independent-set catalogue serves cardinality bounds, weighted bounds, and the
coverage seed; it is not prepared when all three are disabled. Disabling weighted
bounds also disables repricing, while disabling cardinality leaves weighted
bounds available. Capacity constraints still test compatibility of register
classes with fixed assignments even when forward domain filtering is disabled.

Enabling a flag requests an optimization; it does not bypass model verification
or resource limits. Unsupported data and exhausted budgets retain safe weaker
bounds. `--verbose` reports the base profile, effective switches, and cached
preparation/fallback decisions. All profiles preserve reference float semantics,
exactness, ownership, and callback contracts.

The C API uses `pbqp_solve_branch_and_bound` or the `PBQP_SOLVER_BNB` dispatcher
selector for all profiles. The open map's SIZE key `optimization-level` selects
0/1/2, default 1. BOOL keys use the names above without CLI prefixes. Last key
wins, and overrides apply after the profile. See `include/pbqp.h` for the typed
map contract and the [programmer's guide](programmers_guide.md) for examples.

## Applicability at a glance

The scopes below describe what the current implementation enables, not whether
a graph algorithm could theoretically be reused elsewhere:

- **Generic** mechanisms work for arbitrary validated PBQP costs and domains.
- **Structural path only** means the optimized path from section 1 must be
  enabled: the original input matches the pure-interference model and fits
  structural preparation budgets.
- **Model-dependent, shared** mechanisms require that model but are already
  present in baseline `bnb`; sharing code does not make them generic.

| Mechanism | Scope in the current solvers |
| --- | --- |
| Reference-order evaluation; unary/row/matrix minima; general matrix-based forward checking | Generic, shared by `bnb` and the `bnbopt` fallback. |
| Cheap root checks and greedy/reduction incumbents | Generic, shared by both solvers. |
| Prefix planning, cached-bound priorities, resumable DFS, sibling donation, oneTBB dispatch, and approximate search-tree progress | Generic search machinery, shared by both solvers; the supplied bounds strategy determines its estimates. |
| Canonical register naming | Model-dependent and shared; requires verified interchangeable registers, not prepared clique/capacity data. |
| Clique discovery and both structural tail orderings (section 2) | Structural path only. Clique discovery remains inside this path. |
| Register-mask forward checking and clique spill-count DP (section 3) | Structural path only. A clique of arbitrary PBQP edges does not imply distinct registers. |
| Global independent-set register capacity (section 4) | Structural path only. Interpreting graph sets as register classes requires the model. |
| Dual pricing, integer-weight capacity, and its spill DP (section 5) | Structural path only, additionally requiring finite nonnegative spill costs and pricing budgets. |
| Catalogue coverage incumbent (section 6) | Structural path only; requires finite nonnegative spills and a complete catalogue. |
| Prefix-conditioned pricing (section 7) | Structural path only; also requires root weights, a finite incumbent, and per-worker budgets. |
| Dynamic bitsets and maximal-independent-set enumeration in `core` | Reusable graph utilities; their use as PBQP capacity bounds is restricted to the structural path. |

Rejecting the input shape leaves generic search and bounds available. Exceeding
only the optimized graph-storage budget can also leave baseline register symmetry
available when the original pure-interference check succeeded. Pricing-specific
failure leaves other valid structural bounds enabled. These are separate gates,
not one switch that disables every optimization together.

## Baseline and shared search machinery

**Scope: generic, except the explicitly model-dependent register symmetry.**

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

**Scope: eligibility gate for the structural path; generic fallback remains available.**

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

**Scope: structural path only, including clique discovery and both tail modes.**

The optimized solver constructs **one fixed node order during preparation**.
That order is shared by all workers and remains unchanged throughout DFS.
Here "prefix" means nodes already placed in this order, not the current DFS
assignment. "Tail" means all nodes outside the starting clique.

### Starting clique and selection priorities

The solver collects greedy cliques and refines a large clique with bounded
search. The largest discovered clique goes first; ties prefer greater total
spill cost, then a deterministic set order. A maximum clique is not required.
Its vertices are placed one by one using these priorities:

1. More neighbors among nodes already placed in the order.
2. Greater degree in the full interference graph.
3. Greater unary spill cost (option 0).
4. Smaller original node ID.

Within the starting clique the first criterion ties, because every remaining
clique vertex is adjacent to every placed one. The other criteria determine
its internal order. The same priority key is used for the tail, with two ways
of maintaining its first component.

### Available tail orderings

| Verbose diagnostic | How the tail is ordered |
| --- | --- |
| `adaptive prefix connectivity` | Select one remaining vertex using the priorities above, append it, and update neighbor counts before selecting the next. Connectivity includes the starting clique and all tail vertices already placed. |
| `static degree order (comparison budget)` | Sort the whole tail once using the same priorities, then append it in that fixed sequence. At sorting time the placed prefix consists only of the starting clique; later additions do not change the sorted sequence. |

The static diagnostic's "degree order" is abbreviated: its primary key is
still connectivity to the starting clique, followed by full-graph degree,
spill cost, and ID. It is not a sort by degree alone.

For example, placing a tail vertex can increase the priority of one of its
remaining neighbors in adaptive mode. Static mode keeps the neighbor's
position from the initial sort. "Adaptive" describes construction of the
fixed order; it does not enable dynamic variable selection inside DFS.

Selection is automatic; there is no CLI option for choosing a tail mode. Let
`N` be the total node count, including the starting clique. The current rule is

```text
adaptive if N <= word_budget / N
static otherwise
word_budget = 64 * 1024 * 1024
```

This is an overflow-safe estimate of whether quadratic tail selection is
acceptable. With the current budget, adaptive mode covers `N <= 8192`.
Larger graphs use one tail sort to avoid repeated full candidate scans.
The check uses the configured budget, not the work remaining after clique
search. It chooses a preparation algorithm; it does not reject the input or
limit DFS. The starting clique is still placed first in both modes.

### Preparation limits and search semantics

Clique discovery has 64 greedy starts, 100,000 refinement extensions, a
64-million-word ranking budget, and at most 32 retained clique witnesses.
Stopping discovery early only weakens the bound: every retained clique is
valid. These are preparation limits, not input-size limits.

Assignments remain indexed by original node ID. Prefix ranks and DFS cursors
use positions in the prepared order; ordered domain views also serve planning
and progress. This never changes reference cost evaluation, which still adds
unaries in original node ID order and edges in stored order. Canonical register
names follow first appearance in the prepared search order. Option ordering
within a node remains unary cost, then option ID, in both tail modes.

These modes apply when the structural path is enabled. On the generic fallback
path, `bnbopt` uses the baseline node-ID order instead.

## 3. Enforce clique register capacity

**Scope: structural path only.** The bound relies on an interference edge
forbidding equal registers; an arbitrary PBQP edge does not establish this.

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

**Scope: structural path only for the capacity bound.** The underlying
independent-set enumerator is a PBQP-independent graph utility.

A register can hold one independent set of the interference graph. The generic
`IndependentSets` facility in `core` accepts undirected adjacency without PBQP
or register-allocation assumptions. Its only catalogue contains maximal
independent sets by inclusion. A bounded all-subset enumerator lives only in
tests to compare capacities and reproduce the old set-count limitation.
Production enumeration uses iterative pivoted Bron-Kerbosch on the complement
[[1]](#ref-bron-kerbosch), working with the original adjacency to avoid
materializing a second graph. It does not enumerate all subsets and filter
afterward. The pivot maximizes its complement-neighbor count among candidates,
minimizing the immediate branch set; this is the pivot-selection criterion
described by Tomita, Tanaka, and Takahashi [[2]](#ref-tomita). The implementation
uses an explicit stack and preparation budgets, rather than reproducing the
papers' output representation or claiming their full enumeration complexity.

A complete maximal catalogue gives the same per-register capacity as all
independent subsets. Every fixed independent class and feasible extension lies
in some maximal independent superset. Intersecting that superset with the
allowed pending vertices preserves the extension and remains independent. Unit
and auxiliary integer weights are nonnegative, so its score cannot be smaller.
Maximum-cardinality sets alone would be unsafe: a smaller maximal set may have
higher weight or be the only one containing the fixed class. Negative original
spill costs remain valid because the separate reference-order DP handles them.

Preparation retains the 16 MiB auxiliary-storage, 10,000-result, and eight-million
word-work budgets. Storage accounting includes three masks per DFS frame and
reserved catalogue storage; work charges pivot scans, mask construction, copying,
and traversal. Incomplete catalogues are discarded and capacity is disabled;
pricing and other bounds preserve their independent fallback rules.

The private regression isolates one-register, unit-spill K(k,k) root bounds:

| k | All subsets | All-subset complete | Maximal catalogue | Capacity root bound / optimum |
| --- | ---: | --- | ---: | ---: |
| 6 | 126 | Yes | 2 | 6 |
| 12 | 8,190 | Yes | 2 | 12 |
| 13 | More than 10,000 | No | 2 | 13 |
| 16 | More than 10,000 | No | 2 | 16 |

Before this catalogue change, the last two cases fell back to a clique-only
root bound of one. This is a bound-strength comparison, not a whole-solve timing
claim; root proofs and seed discovery can also change the production path.

For each register, find the largest allowed extension of its assigned class
into the unassigned nodes. Ignore competition with other registers and sum
those individual capacities. This overestimates how many nodes can stay in
registers; the positive remainder must spill. The original-order spill-count
DP enforces that global requirement.

## 5. Use auxiliary integer-weight capacity

**Scope: structural path only, with finite nonnegative spill costs and the
additional pricing budgets below.**

This stage designs weights for a register-capacity constraint. It is a bounded
optimization of a continuous dual relaxation, implemented with array operations
rather than an LP package. Its output is **not a pruning cost**. Search uses
nonnegative integer weights, exact catalogue capacities, and the original-order
float cost DP. This distinction matters even if the continuous optimization
happens to converge.

### Model, notation, and eligibility

For the verified pure-interference model, let:

- `V` be the original nodes, with `n = |V|`.
- `R > 0` be the number of registers, excluding spill option 0.
- `c_v` be the original unary spill cost; register unary costs are zero.
- `C` be the complete catalogue of maximal independent sets of the graph.
- `a_I` be the zero/one incidence vector of catalogue set `I`.
- `alpha = max_I |I|` be the maximum independent-set size.

Pricing requires finite, nonnegative `c_v`. Negative or INF spill costs remain
valid PBQP inputs, but disable pricing; generic, clique, and unweighted capacity
bounds still apply. An incomplete catalogue also disables global capacity and
pricing. Preparation occurs only after cheaper root/seed/structural checks.

### Why prices give a real-arithmetic dual bound

Assign each vertex a price `p_v` satisfying `0 <= p_v <= c_v`. Define

```text
s_I(p) = sum(v in I) p_v
m(p)   = max(I in C) s_I(p)
F(p)   = sum(v in V) p_v - R * m(p).
```

One register holds an independent class, whose total price is at most `m(p)`.
Thus all registers together hold at most `R * m(p)` price. The spilled vertices
must carry the rest, and their actual costs dominate their prices because
`p_v <= c_v`. Consequently `F(p)` lower-bounds the **real-arithmetic** cost of
any coloring with spills. Unit capacity weights measure cardinality; admissible
prices can identify expensive bottlenecks that cardinality misses.

This also follows from the covering LP derived below. The Lagrangian and
weak-duality framework is standard; see Boyd and Vandenberghe
[[3]](#ref-boyd-vandenberghe), chapter 5. Let `z_I >= 0` be fractional usage of
independent set `I` as a register, and `s_v >= 0` fractional spilling:

```text
minimize    sum(v in V) c_v * s_v
subject to  s_v + sum(I containing v) z_I >= 1     for every v
            sum(I in C) z_I <= R
            s_v >= 0, z_I >= 0.
```

Every actual assignment maps to a feasible covering solution: extend its
register classes to maximal sets and allow overlapping coverage. Fractional
usage and overlapping coverage make this a relaxation, not a coloring algorithm.
No explicit `s_v <= 1` is needed: with nonnegative costs, excess spilling can
be clipped to one without hurting feasibility or cost.

Use dual variables `p_v >= 0` for coverage and `lambda >= 0` for register count.
The Lagrangian is

```text
sum(v) p_v - R * lambda
  + sum(v) (c_v - p_v) * s_v
  + sum(I) (lambda - s_I(p)) * z_I.
```

Its infimum over nonnegative `s` and `z` is finite when `p_v <= c_v` and
`lambda >= s_I(p)` for every set. The dual is therefore

```text
maximize    sum(v) p_v - R * lambda
subject to  0 <= p_v <= c_v
            s_I(p) <= lambda     for every I in C.
```

Eliminating `lambda` gives the concave maximization of `F(p)` over a box. The
implementation optimizes this reduced expression; it never constructs a generic
LP tableau or invokes simplex/interior-point machinery. Catalogue scans replace
the independent-set maximization oracle.

### A small weighted example

Consider a path `a -- b -- c`, one register, and spill costs `(1, 10, 1)`.
The maximal independent sets are `{a,c}` and `{b}`. Cardinality says that at
least one vertex spills; the cheapest-one-spill relaxation costs only 1.
The actual optimum keeps `b` and spills both ends, costing 2.

Choose prices `(1, 2, 1)`. Both maximal sets have price 2, so `F(p) = 4 - 2 = 2`.
At quantization scale 4, the auxiliary weights become `(4, 8, 4)`. One register
holds at most weight 8; at least weight 8 must spill. Spilling the center costs
10, while spilling both ends costs 2. The weighted spill DP therefore recovers
the stronger bound. These are auxiliary weights, not replacements for costs.

### Why softmax and temperature appear

`m(p)` is a maximum of linear functions. It is continuous but generally not
differentiable when several independent sets tie. Following a single winning
set produces abrupt gradient changes. Instead use log-sum-exp smoothing of
the max-structured objective [[4]](#ref-nesterov); its basic approximation and
convexity properties are also covered in [[3]](#ref-boyd-vandenberghe),
section 3.1.5. Here the smooth maximum is

```text
h_T(p) = T * log(sum(I in C) exp(s_I(p) / T))
F_T(p) = sum(v) p_v - R * h_T(p),                 T > 0.
```

For `K = |C|`, the smoothing satisfies

```text
m(p) <= h_T(p) <= m(p) + T * log(K)
F_T(p) <= F(p) <= F_T(p) + R * T * log(K).
```

Large temperature spreads attention over many sets; small temperature focuses
on near-maximizers and approximates the hard maximum more closely. It also
increases curvature, requiring a smaller gradient step. `T` has the same units
as prices. The fixed temperature schedule is a preparation heuristic, not a
universal PBQP constant or a certified tolerance; rescaling input costs can
change its behavior.

Define softmax probabilities and the gradient:

```text
pi_I(p)        = exp(s_I(p) / T) / sum(J) exp(s_J(p) / T)
g_v(p)         = 1 - R * sum(I containing v) pi_I(p).
```

A vertex present in many high-price register classes receives more of the
register-capacity penalty. The update increases prices where this penalty is
small and decreases them where it is large. The box projection preserves
`0 <= p_v <= c_v`.

`prepare_gradient` subtracts `max_I s_I` before taking exponentials. This leaves
probabilities unchanged, keeps all exponential arguments nonpositive, and avoids
overflow. At least one exponential is one. The code needs only probabilities;
it does not evaluate the logarithm or the smoothed objective explicitly.

### The accelerated projected update

`x` is the feasible price vector; `y` is the extrapolated point at which the
gradient is evaluated. For each temperature, `relax` resets `y = x` and `t = 1`,
then performs 500 iterations:

```text
eta       = T / (R * alpha)
x_next    = clamp(y + eta * g(y), 0, c)          componentwise
t_next    = (1 + sqrt(1 + 4 * t * t)) / 2
beta      = (t - 1) / t_next
y_next    = x_next + beta * (x_next - x)
x, y, t   = x_next, y_next, t_next.
```

The step is not an unexplained tuning coefficient. The Hessian of `h_T` is
`Cov_pi(a_I) / T`. Since `||a_I||^2 = |I| <= alpha`, its spectral norm is at most
`alpha / T`. Hence the gradient of `-F_T` has Lipschitz constant at most
`R * alpha / T`, and `eta` is the reciprocal of that conservative bound.

The `t` and `beta` recurrence is the FISTA accelerated-gradient pattern of
Beck and Teboulle [[5]](#ref-fista), with projection onto the price box as its
proximal step. `y` can leave the box;
it is not a feasible dual witness. Only the projected `x` is retained between
temperature stages. Acceleration need not improve the objective monotonically
at every iteration.

The schedule is `4, 1, 0.25, 0.0625`: successive stages warm-start from `x` and
reset momentum. This gives at most 2,000 iterations, not a convergence or
optimality certificate. After a stage, `run` evaluates the **hard** score `F(x)`.
A nonfinite or nonpositive score stops further investment; it does not prove
infeasibility or even prove that useful prices cannot be found.

### From approximate prices to a safe search constraint

Neither `F(x)` nor `F_T(x)` is compared with the incumbent. Their double sums
are not directed-rounding certificates, and even an exact real-arithmetic dual
bound is not automatically valid for PBQP's sequentially rounded float objective.
Pricing only proposes a vector that may make a necessary capacity condition
stronger.

Let `B = min(32768, floor(4194304 / n))` be the integer state budget and
`P = sum_v x_v`. For positive finite `P`, quantization chooses

```text
scale = min(4, (B - n/2) / P)
w_v   = round(scale * x_v)                      nearest, half ties upward.
```

A nonpositive allowance disables weighted pricing. Reserving `n/2` bounds the
sum of upward rounding errors; the implementation still checks every conversion
and the actual integer total. Nearest rounding has error at most half a unit
per scaled price, whereas truncation has a one-sided error approaching one.
Zero resulting weights, nonfinite values, or an exceeded budget retain the
unweighted bounds. The scaling changes weights, never `pbqp_cost_t` or its
ordinary addition.

During search, let `U` be unassigned vertices, `F_r` the already fixed class for
register `r`, and `A_v` the allowed registers after prefix forward checking.
Recompute, using the complete catalogue and integer arithmetic,

```text
C_r = max(I in C, F_r subset I) sum(v in I intersect U, r in A_v) w_v
D   = max(0, sum(v in U) w_v - sum(r) C_r).
```

Each actual register extension contributes at most `C_r`. Ignoring competition
between registers can only overestimate the price they can accommodate.
Therefore at least weight `D` must spill. This condition is valid for **any**
nonnegative integer weights, irrespective of the optimizer's convergence,
softmax rounding errors, or closeness to a dual optimum. Rounded weights need
not satisfy the original monetary box `w_v <= c_v`.

The spill DP caps its state at `D`. A register transition adds weight zero and
original cost zero; a spill transition adds `w_v` and the original `c_v`.
It also processes fixed unary terms, in original node ID order. Feasibility is
separate from the rounded float score, including reachable infinite sums.
This necessary-condition DP supplies the pruning bound. Root weights are
prepared once and shared read-only. Section 7 adds worker-local weights refined
for the current prefix, using the same integer-capacity certificate. Clique,
unweighted, root-weighted, and refined constraints are combined by maximum,
not by adding overlapping costs.

### Catalogue sensitivity, budgets, and code map

A complete maximal catalogue gives the same hard capacity maximum as all
independent subsets for nonnegative weights. It does **not** give the same
softmax probabilities. At the initial zero vector every listed set has equal
probability; removing dominated subsets changes vertex frequencies immediately.
Finite-budget optimization can select different prices even when the hard
objectives agree. Quantization can amplify that difference, and equal root
bounds do not imply equal search trees. Neither smaller catalogues nor nearest
rounding guarantee a faster solve for every input.

The same maximal catalogue serves pricing and search. There is no second
all-subset catalogue for selected fixtures. Preparation has an eight-million
pattern-vertex visit estimate: the code charges up to 4,004 passes over catalogue
entries (scoring and gradient accumulation at each iteration, plus stage checks).
The integer total is capped at 32,768 and the estimated weighted DP scan at
4,194,304 state slots. Exhausted independent-set budgets discard the entire
catalogue; pricing-specific limits disable only the weighted path. See
`--verbose` for the actual catalogue, iteration count, quantization scale,
state limit, and fallback reason. Prefix diagnostics also report candidate
storage, refresh work/iteration budgets, and the prepared state limit.

The code map refers to [bnb_pricing.cppm](../src/solvers/bnb/bnb_pricing.cppm)
and [bnb_bounds.cppm](../src/solvers/bnb/bnb_bounds.cppm).

| Implementation | Mathematical role |
| --- | --- |
| `PriceOptimizer::score_patterns` | Compute `s_I(p)` and the hard maximum. |
| `PriceOptimizer::prepare_gradient` | Stable softmax and `g(y)`. |
| `PriceOptimizer::update` | Box projection and momentum extrapolation. |
| `PriceOptimizer::relax`, `run` | Step bound, temperature continuation, bounded stopping. |
| `quantize_prices` | Checked conversion into nonnegative integer weights. |
| `RegallocPricing::prepare` | Eligibility, work/state limits, and weight ownership. |
| `BranchAndBoundBounds::capacity_bound` | Prefix-conditioned integer register capacities and spill requirement. |
| `BranchAndBoundBounds::spill_bound` | Original-order float DP used for pruning. |

The references explain the general mechanisms. The covering relaxation,
capacity interpretation, budgets, quantization, and reference-order pruning
pipeline above describe this implementation. They do not assert an LP solver's
certificate or a convergence rate for the entire bounded, multi-temperature PBQP
preparation stage.

## 6. Seed from independent-set coverage

**Scope: structural path only.** The complete maximal catalogue is also a
primal representation of the register-allocation problem. Choose at most `R`
sets, take their union as the retained vertices, and assign each vertex to its
first selected set's register. Each register class is a subset of an independent
set, so overlapping selected sets are harmless. Vertices outside the union spill.
The catalogue is shared with both pricing paths; this creates no second catalogue.

[RegisterCoverSeed](../src/solvers/bnb/bnb_cover.cppm) uses a deterministic
bounded search over this representation. It starts with maximum uncovered spill
cost gains, tries perturbed-cost greedy restarts, and exchanges one or two
register classes. Two-class exchanges consider the 16 largest marginal-gain
candidates. Three of every four attempts instead remove two to four classes
from the best cover and greedily rebuild them before local improvement.

Limits are 128 attempts, 32 one-class passes, four two-class passes, and
8,388,608 pattern-member visits overall. Exceeding a budget retains the best
completed candidate. Reaching the already proved root lower bound stops early.
Double arithmetic ranks candidates only: each saved assignment is evaluated
against the original input with the shared reference-order float evaluator.
Reachable overflowed costs remain feasible. Negative or nonfinite spill costs
disable this seed; ordinary greedy and reduction remain available.

Orchestration checks the prepared capacity bound before constructing this seed,
and checks again after an incumbent improvement. Root proofs therefore skip
unnecessary heuristic work. This is an internal incumbent seed, not a new public
solver or an optimality claim by the heuristic itself.

## 7. Reprice the current prefix

**Scope: structural path only.** This path requires complete independent sets,
successful root pricing, a finite incumbent, and its own scratch/work budgets.
A root price vector can be nearly optimal for the original graph yet very weak
after several register classes have been fixed. Improving quantization alone
cannot recover the information lost by keeping those prices fixed.

Let `U` be the unassigned vertices and `H_r` the vertices already in register
`r`. The compatible catalogue for that register is

```text
C_r = { I in C : H_r is a subset of I }.
```

An unused register has `H_r = empty` and uses the whole catalogue. Candidate
scores include only vertices in `U`. With pending prices `0 <= p_v <= c_v`,
the conditional real-arithmetic dual objective is

```text
F_A(p) = fixed real spill cost + sum(v in U) p_v
         - sum(r) max(I in C_r) sum(v in I intersect U) p_v.
```

Replace each register's maximum with its own log-sum-exp, using the same
smoothing and accelerated projected update as section 5. The pending gradient is

```text
g_v = 1 - sum(r) sum(I in C_r containing v) softmax_r(I).
```

Assigned prices remain zero. Registers not yet used share one catalogue and one
softmax with multiplicity equal to their count. This avoids repeating identical
work for every unused register. If `alpha_U` is the largest pending pattern size,
the sum of covariance Hessians has norm at most `R * alpha_U / T`; the safe
heuristic step remains `T / (R * alpha_U)`. Warm starts use the root weights
converted back to price units and projected into the original cost box.

[PrefixPriceOptimizer](../src/solvers/bnb/bnb_prefix_pricing.cppm) performs
at most 64 iterations at each of the four root temperatures. After each stage,
checked quantization writes a prepared integer buffer. The ordinary per-register
integer capacities and original-order spill DP test those weights. **Neither
`F_A`, its fixed-cost offset, nor a softmax value is a pruning bound.** Stop
refining when this DP already closes the prefix against the incumbent.

Refinement starts after the initial clique's positions have been assigned.
Within that scope it is refreshed after four more assigned positions, when
backtracking changes the prefix used to obtain the prices, or when the incumbent
improves. The bounds workspace owns this state; shared graph/catalogue/root
weights remain immutable. Each bound can use both root and local weights.
Previously cached child bounds remain valid even after local prices change:
every nonnegative integer weight vector defines a necessary capacity condition,
independently of the prefix from which its prices were learned.

Each worker preallocates at most 65,536 register-pattern candidate slots. A
refresh's iterations share a 2,097,152-unit work budget, charging scoring and
gradient visits plus per-candidate bookkeeping. Candidate construction is bounded
separately by the slot cap. Exhausting this heuristic budget can retain a
valid partially refined vector. Exceeding the candidate-storage budget disables
refinement and retains root weights. Auxiliary totals obey the existing 32,768
state limit and 4,194,304-slot DP budget; actual arrays use a checked bound from
spill costs and the maximum quantization scale. No refresh allocates memory or
invokes diagnostic callbacks inside DFS.

Example [10_repricing_opts.pbqp](../examples/pbqp/10_repricing_opts.pbqp) has 70 vertices, 16 registers, and
395 maximal independent sets. It illustrates both changes: a coverage seed is
much better than the ordinary greedy seed, and a fixed prefix can require very
different prices. The exact solver returns reference-order cost `369.419556`.
This is a benchmark fixture, not a routine full-search CTest. Root and prefix
pricing use the same catalogue and numerical proof throughout.

## Diagnostics and verification

`pbqp --solve=bnbopt --verbose INPUT` reports which paths are available and
used, together with reasons for skipped or limited preparation. Completing a
cheap proof can make later optimizations unnecessary. Exhausting a clique
search budget preserves valid witnesses; exhausting independent-set enumeration
instead disables capacity bounds. These decisions must remain distinct. Verbose
output includes the actual number of retained cliques, examined sets, pricing
iterations, effective integer-weight
limit and scaling, planned jobs, prepared workers, and any planning cutoff.
A final preliminary lower/upper interval is printed before DFS, including an
explicit distinction between no feasible incumbent and a feasible infinite-cost
one; see [solver diagnostics](solvers.md#verbose-diagnostics). Independent-set
preparation reports its result and work count or budget failure, rather than
announcing a catalogue-kind choice or a pending preparation stage.
Library callers set the `diagnostics` and `diagnostic-data` keys in `pbqp_solve_options`
passed to `pbqp_solve_branch_and_bound` with `optimization-level=2`;
CLI formatting stays in the tool and diagnostic text never replaces stdout
solution records.

Private tests compare the maximal catalogue and test-only all-subset reference
against subset predicates,
per-register capacities on every small fixed-class/allowed-mask combination,
and bounds against independent exhaustive completion of canonical prefixes. They
cover signed and fractional costs, explicit INF, cancellation, reachable
infinite accumulated costs, auxiliary-weight scaling, and fallback behavior.
Conditioned-price tests replay learned vectors at unrelated prefixes and at the
root, check register/node masks across word boundaries, and exercise candidate
budget fallback. Coverage seeds are independently evaluated, including feasible
infinite accumulated scores and unsupported signed/INF spill costs.
Parallel tests cover actual sibling donation, serialized progress, weight
conservation, and queue cancellation on dispatcher failure. Public tests retain independent
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
edges, and 16 registers. Its graph has 441 nonempty independent subsets, represented by 181 maximal
sets, and none is larger than three, so at least 12 nodes must spill. Its weighted optimum instead
spills 13 nodes and has reference cost `338.854034`.

The maximum clique has size 19. Its initial clique relaxation is much weaker
than global weighted capacity, making this example useful for evaluating the
additional bounds. Measurements depend on preparation, incumbent discovery,
frontier shape, and scheduling; use the benchmark rather than asserting timing
ratios in correctness tests. The other exact-solver benchmark cases remain
useful for detecting overhead on easier inputs.


## References

The citations in the text link to the numbered entries below. These are sources
for the general graph and optimization methods; implementation-specific
adaptations and correctness arguments are given in the preceding sections.

1. <a id="ref-bron-kerbosch"></a> Coen Bron and Joep Kerbosch.
   [*Algorithm 457: Finding All Cliques of an Undirected Graph*](https://doi.org/10.1145/362342.362367).
   *Communications of the ACM*, 16(9):575–577, 1973.
   Original maximal-clique enumeration algorithm; maximal independent sets are
   enumerated as maximal cliques of the complement.

2. <a id="ref-tomita"></a> Etsuji Tomita, Akira Tanaka, and Haruhisa Takahashi.
   [*The worst-case time complexity for generating all maximal cliques and computational experiments*](https://doi.org/10.1016/j.tcs.2006.06.015).
   *Theoretical Computer Science*, 363(1):28–42, 2006.
   See section 3 for the pivot that maximizes candidate-neighbor intersection
   and reduces the set of branches explored next.

3. <a id="ref-boyd-vandenberghe"></a> Stephen Boyd and Lieven Vandenberghe.
   [*Convex Optimization*](https://web.stanford.edu/~boyd/cvxbook/).
   Cambridge University Press, 2004.
   Section 3.1.5 covers log-sum-exp and its relation to the maximum; chapter 5
   develops Lagrangian duality. The authors' page provides the complete book.

4. <a id="ref-nesterov"></a> Yu. Nesterov.
   [*Smooth minimization of non-smooth functions*](https://doi.org/10.1007/s10107-004-0552-5).
   *Mathematical Programming*, 103:127–152, 2005.
   Background on smoothing objectives with an explicit maximum structure.

5. <a id="ref-fista"></a> Amir Beck and Marc Teboulle.
   [*A Fast Iterative Shrinkage-Thresholding Algorithm for Linear Inverse Problems*](https://doi.org/10.1137/080716542).
   *SIAM Journal on Imaging Sciences*, 2(1):183–202, 2009.
   Source of the accelerated proximal-gradient recurrence used for the
   projected price updates.
