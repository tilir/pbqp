# Variable elimination for PBQP

Implementation notes from Peyrard et al., *Exact or approximate inference in
graphical models: why the choice is dictated by the treewidth, and how variable
elimination can be exploited*, §§3.2–4. DOI: 10.1111/anzs.12257.

## Plan the order

1. Start from the primal graph. Each node is a variable and each pairwise term
   connects its two variables. Unary terms add no edges.
2. Choose an active vertex. Its active neighbors form its separator. Record the
   separator, add every missing edge within it, and remove the vertex.
3. Repeat until the graph is empty. The maximum separator size is the induced
   width of this order. Treewidth is the minimum induced width over all orders.
   Minimum degree and minimum fill are practical ordering heuristics. The
   implementation uses minimum current degree with node ID as a tie breaker.

## Eliminate and reconstruct

1. Store each unary or pairwise table in the bucket of the earliest variable
   in its scope. Store each generated factor in the same way.
2. For vertex `x` and separator assignment `s`, compute
   `next[s] = min_x (sum of all factors in the bucket evaluated at (x,s))`.
   Save one minimizing value of `x` for each finite output. Evaluate every
   consumed table using its own scope and row-major strides. A missing edge
   contributes zero, and an INF entry forbids the candidate.
3. Replace the bucket with the generated table. An empty separator yields a
   scalar. If that scalar is INF, the whole instance is infeasible. Finite
   scalars need no storage for finding the assignment and can be omitted from
   later minimizations, including across disconnected components.
4. Walk the order in reverse. Separator variables are already assigned.
   Index the saved argmin table and assign `x`. Evaluate the final tuple in the
   original PBQP term order to obtain its API cost.

For separator `S`, one output table has `P = product(d[y], y in S)`
entries, and each entry makes a reduction over `d[x]` candidates. The inner
work is at most `P * d[x] * bucket_factor_count` factor lookups/additions.
For uniform domain size `D` and induced width `w`, the largest table has
`D^w` entries and the largest candidate grid has `D^(w+1)` points.
Reconstruction keeps one argmin per generated entry. Cost tables can be
released as soon as their destination bucket is processed.

## Implementation boundary

Each output cell is independent within a bucket. Precompute strides on the
host and divide the row-major output into contiguous blocks. Join workers
before using a generated factor in another bucket. The current implementation
runs the kernel on the CPU using oneTBB.

The elimination identity relies on associativity of addition and distributivity
of minimum over addition. PBQP's reference objective uses sequential float
addition. The implementation certifies exact representability of every finite
partial sum before applying the identity. With lattice unit `2^e`, it bounds
the sum of per-factor maximum magnitudes by both `2^24` lattice units for
binary32 and the largest finite float. This is an eligibility check. The
kernel uses ordinary double addition and reevaluates the final assignment
in reference float order. An uncertified result carries FEASIBLE quality
and triggers a warning before factor computation. See the solver guide for
preparation budgets. [Solver guide](../doc/solvers.md#bucket-elimination).
