# PBQP text formats

A problem contains discrete node options, unary costs, and pairwise costs on
edges. A solution selects one option per node. `INF` forbids an option or pair;
missing edges contribute zero. The current text format is **PBQP 2**, written
as `PBQP 2` (an integer version token). The reader also accepts legacy PBQP 1.
Text-format versions are independent of the C API version.

## PBQP 2 file structure

```text
PBQP 2
NODES N
EDGES M
MATRICES T
NODE 0 d_0
<d_0 costs>
...
NODE N-1 d_(N-1)
<d_(N-1) costs>
MATRIX 0 rows cols DENSE
<rows * cols costs>
...
MATRIX T-1 rows cols SPARSE default_cost K
<exactly K triples: row col cost>
EDGE src tgt matrix_id
...
END
```

The order is header counts, all nodes, all matrix definitions, then all edges.
Keywords are case-sensitive. Counts, IDs, dimensions, and sparse coordinates
are unsigned decimal integers fitting in `size_t`; matrix products and byte
sizes must also fit. Zero nodes, edges, or matrix definitions are allowed.

- Exactly `N` nodes appear, numbered consecutively from zero. Each option count
  is positive; different nodes may have different domains.
- Exactly `T` matrices appear, numbered consecutively from zero. Both dimensions
  are positive. Every definition is independent; matrices cannot reference
  other matrices.
- Exactly `M` edges follow. Each has `0 <= src < tgt < N`; loops and repeated
  endpoint pairs are invalid. Edge order is otherwise arbitrary and significant
  to floating-point evaluation. Serialization preserves it.
- Each edge references an existing matrix. Its row count must equal the source
  option count and its column count must equal the target option count.
  Dimensions are checked separately, not only by their product. No transpose
  or endpoint reversal is implicit.
- Several edges may reference one matrix. Unused definitions are allowed; they
  are omitted when the public problem view is serialized again.
- `END` terminates the problem. Only whitespace and comments may follow it.

### Dense matrices

`MATRIX id rows cols DENSE` is followed by exactly `rows * cols` costs, in
row-major order. Cell `(a, b)` describes source option `a` and target option `b`;
its flat index is `a * cols + b`. Options and cell coordinates start at zero.

### Sparse matrices

`MATRIX id rows cols SPARSE default_cost K` assigns `default_cost` to every
cell, followed by exactly `K` triples replacing individual cells. Coordinates
must be in range and distinct; triples may appear in any order. `K` may be zero
and cannot exceed the cell count. Redundant replacements equal to the default
are valid. Default and replacement costs follow the same finite/INF rules as
all other costs; the default may be INF, negative, or signed zero.

For example, two interchangeable registers plus spill can use:

```text
MATRIX 0 3 3 SPARSE 0 2
1 1 INF
2 2 INF

EDGE 0 1 0
EDGE 0 2 0
```

The encoding has no register-allocation-specific semantics. Arbitrary coalescing
costs, heterogeneous domains, and matrices with predominantly INF or nonzero
entries use the same grammar. Each referenced matrix is materialized once as a
dense array for the existing C API; parsed edges share its read-only storage.
No archive codec or external decompression step is involved.

## PBQP 1 compatibility

PBQP 1 has the same node and edge rules but no matrix table:

```text
PBQP 1
NODES N
EDGES M
NODE 0 d_0
<d_0 costs>
...
EDGE src tgt
<d_src * d_tgt costs>
...
END
```

All nodes precede all edges. Each edge owns its inline row-major costs in the
file. `MATRICES`, matrix definitions, and matrix-reference edge fields are not
accepted under signature `PBQP 1`. Too few or too many costs are invalid in
either version. The empty problem has one empty solution costing zero.

## Costs and lexical rules

A finite cost is a decimal floating-point number with an optional leading
`+` or `-`, an optional decimal point, and an optional exponent introduced by
`e` or `E`. Examples: `0`, `-3`, `+0.5`, `.25`, `1.`, `-1.25e+2`.
Negative costs are permitted. Infinity is written only as the separate token
`INF`. Hexadecimal numbers, decimal commas, NaN, and other infinity spellings
(including `-INF`) are invalid.

The C API defines `typedef float pbqp_cost_t`; `PBQP_INF` is positive infinity.
Finite input values are rounded to `float`. Numeric literals outside the
representable range, including underflow reported by the parser, produce
`PBQP_OVERFLOW`; use `INF` to express a forbidden choice explicitly.

Cost addition uses ordinary floating-point `+`, exposed through the inline
`pbqp_cost_add(a, b)` function. It performs no overflow checks or saturation.
The solver adds unary costs in node ID order, then pairwise costs in stored
edge order. Rounding and arithmetic overflow follow the platform's float
behavior. Arithmetic overflow does not return `PBQP_OVERFLOW`. A tuple
containing an explicit `INF` term is infeasible regardless of its other costs.

`PBQP_SOLUTION_OPTIMAL` means minimal under this sequential float evaluation
order, not under the exact real sum of the stored floats. For example, in
binary32 round-to-nearest arithmetic, both `16777216 + 1 - 16777216` and
`16777216 + 0 - 16777216` evaluate to zero. The exhaustive solver therefore
keeps the first of these tied assignments, although their exact sums differ.
Changing the summation order may change which assignment is best.

`pbqp_evaluate_assignment` checks indices and evaluates an assignment under
this common policy. Its `is_feasible` output distinguishes an explicit INF
term from an allowed assignment whose sum overflows to infinity. A forbidden
assignment is not proof that the entire problem is infeasible.

All whitespace characters are equivalent. Line breaks in vectors and matrices
are for readability only. A comment starts with `#` and extends to the end of
the line or file. `#` also terminates an immediately preceding token.
Comments may appear anywhere between tokens.

## Example

```text
PBQP 2
NODES 2
EDGES 1
MATRICES 1

NODE 0 2
-1 3

NODE 1 3
0 INF 2

MATRIX 0 2 3 DENSE
5 0 INF
1 0 -2

EDGE 0 1 0
END
```

The matrix forbids pair `(0, 2)`, and node 1 forbids its option 1. The optimal
tuple is `(1, 2)`, with total cost `3 + 2 - 2 = 3`.

## Serialization policy

`pbqp_write` always emits PBQP 2. It preserves node/edge order and exact float
values, including the sign of zero. Matrices are interned by both dimensions
and values, with IDs assigned by first appearance in edge order. Equal arrays
need not already share a pointer to be interned. Different signed zeros remain
distinct; no arithmetic or graph reduction is performed.

With `K` exceptions and `C` cells, SPARSE is chosen when a default value gives
`3*K + 2 < C`, comparing payload token counts without overflowing. Such a value
must be a strict majority and is unique. A majority-vote pass and an exception
count verify it with constant scratch instead of a per-cell frequency table.
Otherwise DENSE is used. Sparse replacements are
written in row-major order. This deterministic policy is not a claim of minimum
possible byte length.

Finite costs use the classic locale and `max_digits10` precision so reading a
dump recovers the same float values. Original comments and unused definitions
are not preserved. The generator appends fresh version/command comments after
`END`; see [reproducibility](generation.md#reproducibility-and-provenance).
