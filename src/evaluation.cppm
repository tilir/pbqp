/*
 * Shared reference-order assignment evaluation.
 *
 * Evaluates already validated problems and assignments without allocation.
 * Explicit prohibitions are separate from floating-point sum overflow.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"

module pbqp:evaluation;

bool evaluate_assignment(const pbqp_problem &p, const size_t *tuple,
                         pbqp_cost_t &cost) noexcept {
  cost = 0;
  for (size_t k = 0; k < p.num_nodes; ++k) {
    pbqp_cost_t c = p.nodes[k].costs[tuple[k]];
    if (c == PBQP_INF) {
      cost = PBQP_INF;
      return false;
    }
    cost = pbqp_cost_add(cost, c);
  }
  for (size_t k = 0; k < p.num_edges; ++k) {
    const auto &e = p.edges[k];
    pbqp_cost_t c =
        e.costs[tuple[e.src] * p.nodes[e.tgt].num_options + tuple[e.tgt]];
    if (c == PBQP_INF) {
      cost = PBQP_INF;
      return false;
    }
    cost = pbqp_cost_add(cost, c);
  }
  return true;
}
