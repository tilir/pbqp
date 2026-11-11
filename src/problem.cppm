/*
 * Problem validation and checked dimensions.
 *
 * Implements structural validation and array-size checks shared by text I/O.
 * Validation failures use the internal exception boundary, outside solver
 * loops.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;

#include "pbqp.h"
#include <cmath>
#include <limits>
#include <set>
#include <utility>

module pbqp:problem;
import :failure;

template <class T> void check_count(size_t n, size_t line = 0) {
  if (n > std::numeric_limits<size_t>::max() / sizeof(T))
    fail(PBQP_OVERFLOW, "allocation size overflow", line);
}

void validate_costs(const pbqp_cost_t *costs, size_t count) {
  for (size_t k = 0; k < count; ++k)
    if (!std::isfinite(costs[k]) && costs[k] != PBQP_INF)
      fail(PBQP_INVALID, "costs must be finite or positive infinity");
}

size_t matrix_size(const pbqp_problem &p, const pbqp_edge &e, size_t line = 0) {
  size_t rows = p.nodes[e.src].num_options, cols = p.nodes[e.tgt].num_options;
  if (rows > std::numeric_limits<size_t>::max() / cols)
    fail(PBQP_OVERFLOW, "matrix dimensions overflow", line);
  check_count<pbqp_cost_t>(rows * cols, line);
  return rows * cols;
}

void validate(const pbqp_problem *p) {
  if (!p || (p->num_nodes && !p->nodes) || (p->num_edges && !p->edges))
    fail(PBQP_INVALID, "missing problem or arrays");
  check_count<pbqp_node>(p->num_nodes);
  check_count<pbqp_edge>(p->num_edges);
  for (size_t k = 0; k < p->num_nodes; ++k) {
    if (!p->nodes[k].num_options || !p->nodes[k].costs)
      fail(PBQP_INVALID, "node needs a positive option count and costs");
    check_count<pbqp_cost_t>(p->nodes[k].num_options);
    validate_costs(p->nodes[k].costs, p->nodes[k].num_options);
  }
  std::set<std::pair<size_t, size_t>> edges;
  for (size_t k = 0; k < p->num_edges; ++k) {
    const auto &e = p->edges[k];
    if (e.src >= e.tgt || e.tgt >= p->num_nodes)
      fail(PBQP_INVALID, "edge must satisfy 0 <= src < tgt < NODES");
    if (!edges.emplace(e.src, e.tgt).second)
      fail(PBQP_INVALID, "duplicate edge");
    auto count = matrix_size(*p, e);
    if (!e.costs)
      fail(PBQP_INVALID, "missing edge costs");
    validate_costs(e.costs, count);
  }
}
