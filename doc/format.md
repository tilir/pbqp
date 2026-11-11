# PBQP 1 text format

A problem consists of nodes with discrete options, unary costs, and pairwise
cost matrices on edges. A solution selects one option per node and minimizes
the sum of selected unary and pairwise costs. `INF` forbids a node option or
an option pair. Any tuple selecting an `INF` term is infeasible. Missing edges
contribute zero for every option pair.

## File structure

```text
PBQP 1
NODES N
EDGES M
NODE 0 d_0
<d_0 costs>
...
NODE N-1 d_(N-1)
<d_(N-1) costs>
EDGE src tgt
<d_i * d_j costs>
...
END
```

- `PBQP 1` is the signature and version. Keywords are case-sensitive.
- `NODES N` and `EDGES M` declare the node and edge counts.
- Exactly `N` node records appear first, in increasing ID order, from 0 to `N-1`.
- `NODE i d` declares node `i` with positive option count `d`, followed by
  exactly `d` costs. Different nodes may have different option counts.
- Exactly `M` edge records follow all nodes, in any order. Each `EDGE src tgt`
  must satisfy `0 <= src < tgt < N`. Self-loops and duplicate edges are invalid.
- Edges are undirected; `src` and `tgt` define the matrix orientation.
- Each edge is followed by exactly `d_src * d_tgt` costs in row-major order.
  Rows represent options of node `src`; columns represent options of node `tgt`.
  Options start at zero. The flat index for options `(a, b)` is `a * d_tgt + b`.
- `END` terminates the problem. Only whitespace and comments may follow it.

Counts, node IDs, and dimensions are unsigned decimal integers that fit in
`size_t`. Zero nodes and zero edges are valid: the empty problem has one empty
solution with cost zero. Records with too few or too many costs are invalid.

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

All whitespace characters are equivalent. Line breaks in vectors and matrices
are for readability only. A comment starts with `#` and extends to the end of
the line or file. `#` also terminates an immediately preceding token.
Comments may appear anywhere between tokens.

## Example

```text
PBQP 1
NODES 2
EDGES 1

NODE 0 2
-1 3

NODE 1 3
0 INF 2

EDGE 0 1
5 0 INF  # Option 0 of node 0
1 0 -2   # Option 1 of node 0

END
```

The edge forbids pair `(0, 2)`, and node 1 forbids its option 1. The optimal
tuple is `(1, 2)`, with total cost `3 + 2 - 2 = 3`.

`pbqp_write` preserves the problem structure and costs, normalizes whitespace,
and writes matrices one row per line. Finite costs are written with
`max_digits10` significant digits, using the classic locale, so reading a dump
recovers the same float values. Original comments are not preserved.
