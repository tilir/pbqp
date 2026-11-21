/*
 * Shared recognition of uniform zero-register-cost interference problems.
 * Checks every original unary and matrix; this is a sufficient model, not
 * a general symmetry test. Inputs have already passed structural validation.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
module pbqp:interference;

namespace pbqp {
enum class InterferenceFailure {
  None,
  Disabled,
  Empty,
  NoRegisters,
  Domains,
  RegisterCost,
  MatrixCost
};

struct InterferenceCheck {
  InterferenceFailure failure = InterferenceFailure::None;
  size_t node = 0, option = 0, edge = 0, row = 0, col = 0;
  pbqp_cost_t actual = 0, expected = 0;
};

bool is_pure_interference_matrix(const pbqp_edge &edge, size_t domain,
                                 InterferenceCheck &result) noexcept {
  for (size_t row = 0; row < domain; ++row)
    for (size_t col = 0; col < domain; ++col) {
      auto expected = row && row == col ? PBQP_INF : 0;
      if (edge.costs[row * domain + col] != expected) {
        result.failure = InterferenceFailure::MatrixCost;
        result.row = row;
        result.col = col;
        result.actual = edge.costs[row * domain + col];
        result.expected = expected;
        return false;
      }
    }
  return true;
}

InterferenceCheck
check_interference_structure(const pbqp_problem &problem) noexcept {
  // This recognizes the pure model, not every register-label symmetry.
  InterferenceCheck result;
  if (!problem.num_nodes) {
    result.failure = InterferenceFailure::Empty;
    return result;
  }
  if (problem.nodes[0].num_options < 2) {
    result.failure = InterferenceFailure::NoRegisters;
    return result;
  }
  size_t domain = problem.nodes[0].num_options;
  for (size_t id = 0; id < problem.num_nodes; ++id) {
    result.node = id;
    const auto &node = problem.nodes[id];
    if (node.num_options != domain) {
      result.failure = InterferenceFailure::Domains;
      return result;
    }
    for (size_t option = 1; option < domain; ++option)
      if (node.costs[option] != 0) {
        result.failure = InterferenceFailure::RegisterCost;
        result.option = option;
        result.actual = node.costs[option];
        return result;
      }
  }
  for (size_t id = 0; id < problem.num_edges; ++id) {
    result.edge = id;
    if (!is_pure_interference_matrix(problem.edges[id], domain, result))
      return result;
  }
  return result;
}

} // namespace pbqp
