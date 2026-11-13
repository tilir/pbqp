/*
 * LLVM-style R0/R1/R2/RN reduction and reverse assignment heuristic.
 *
 * Preparation clones and reduces the graph, allocating R2 fill matrices as
 * needed. Backpropagation uses frozen costs and adjacency without allocation.
 * Final costs and feasibility are checked against the borrowed input problem.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <utility>
#include <vector>

module pbqp:reduction;
import :error;
import :evaluation;
import :ownership;
import :reduction_graph;
import :reduction_worklist;

namespace pbqp {
class ReductionSearch {
  const pbqp_problem &problem;
  ReductionGraph graph;
  SolutionPtr solution;
  std::vector<size_t> order;

  bool reduce_one(size_t id) noexcept;
  size_t reduce_two(size_t id);
  bool assign(size_t id) noexcept;

public:
  explicit ReductionSearch(const pbqp_problem &p) : problem(p), graph(p) {}
  void prepare();
  pbqp_status run(pbqp_error *error) noexcept;
  const pbqp_solution &get_solution() const noexcept { return *solution; }
  pbqp_solution *release_solution() noexcept { return solution.release(); }
};

bool ReductionSearch::reduce_one(size_t id) noexcept {
  bool feasibility_changed = false;
  size_t edge = graph.node(id).edges.front();
  size_t neighbor = graph.other(edge, id);
  auto &neighbor_costs = graph.node(neighbor).costs;
  const auto &costs = graph.node(id).costs;
  for (size_t option = 0; option < neighbor_costs.size(); ++option) {
    ReductionCost minimum{0, false};
    for (size_t eliminated = 0; eliminated < costs.size(); ++eliminated) {
      auto score = add_reduction_costs(graph.cost(edge, id, eliminated, option),
                                       costs[eliminated]);
      if (better_reduction_cost(score, minimum))
        minimum = score;
    }
    auto updated = add_reduction_costs(neighbor_costs[option], minimum);
    feasibility_changed |= updated.feasible != neighbor_costs[option].feasible;
    neighbor_costs[option] = updated;
  }
  return feasibility_changed;
}

size_t ReductionSearch::reduce_two(size_t id) {
  size_t first_edge = graph.node(id).edges[0];
  size_t second_edge = graph.node(id).edges[1];
  size_t src = graph.other(first_edge, id);
  size_t tgt = graph.other(second_edge, id);
  if (src > tgt) {
    std::swap(src, tgt);
    std::swap(first_edge, second_edge);
  }
  size_t rows = graph.node(src).costs.size();
  size_t cols = graph.node(tgt).costs.size();
  std::vector<ReductionCost> delta(ReductionGraph::matrix_size(rows, cols));
  const auto &costs = graph.node(id).costs;
  for (size_t row = 0; row < rows; ++row)
    for (size_t col = 0; col < cols; ++col) {
      ReductionCost minimum{0, false};
      for (size_t eliminated = 0; eliminated < costs.size(); ++eliminated) {
        auto score = add_reduction_costs(
            add_reduction_costs(graph.cost(first_edge, id, eliminated, row),
                                graph.cost(second_edge, id, eliminated, col)),
            costs[eliminated]);
        if (better_reduction_cost(score, minimum))
          minimum = score;
      }
      delta[row * cols + col] = minimum;
    }
  return graph.merge_edge(src, tgt, std::move(delta));
}

void ReductionSearch::prepare() {
  solution = make_solution(problem.num_nodes);
  order.reserve(problem.num_nodes);
  ReductionWorklist worklist(graph);
  while (order.size() < graph.num_nodes()) {
    size_t id = worklist.next_node();
    graph.freeze(id);
    switch (graph.degree(id)) {
    case 1: {
      size_t neighbor = graph.other(graph.node(id).edges.front(), id);
      bool changed = reduce_one(id);
      worklist.costs_changed(neighbor, changed);
      break;
    }
    case 2:
      worklist.refresh_edge(reduce_two(id));
      break;
    default:
      // R0 and RN preserve costs for reverse assignment without propagation.
      break;
    }
    for (size_t edge : graph.node(id).edges)
      worklist.remove_edge(edge);
    graph.disconnect(id);
    // R0 has no incident edge to mark it as removed from the queues.
    worklist.costs_changed(id, false);
    worklist.flush();
    order.push_back(id);
  }
}

bool ReductionSearch::assign(size_t id) noexcept {
  const auto &node = graph.node(id);
  ReductionCost best{0, false};
  size_t chosen = 0;
  for (size_t option = 0; option < node.costs.size(); ++option) {
    auto score = node.costs[option];
    for (size_t edge : node.edges)
      score = add_reduction_costs(
          score, graph.cost(edge, id, option,
                            solution->options[graph.other(edge, id)]));
    if (better_reduction_cost(score, best)) {
      best = score;
      chosen = option;
    }
  }
  if (!best.feasible)
    return false;
  solution->options[id] = chosen;
  return true;
}

pbqp_status ReductionSearch::run(pbqp_error *error) noexcept {
  for (auto node = order.rbegin(); node != order.rend(); ++node)
    if (!assign(*node))
      return set_error(error, PBQP_NO_SOLUTION, 0,
                       "reduction backpropagation reached a dead end; "
                       "infeasibility is not proven");
  if (!evaluate_assignment(problem, solution->options, solution->cost))
    return set_error(
        error, PBQP_NO_SOLUTION, 0,
        "reduction assignment is forbidden; infeasibility is not proven");
  solution->quality = PBQP_SOLUTION_FEASIBLE;
  return PBQP_OK;
}
} // namespace pbqp
