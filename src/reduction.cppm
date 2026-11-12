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
#include <set>
#include <utility>
#include <vector>

module pbqp:reduction;
import :error;
import :evaluation;
import :ownership;
import :reduction_graph;

class ReductionSearch {
  const pbqp_problem &problem;
  ReductionGraph graph;
  SolutionPtr solution;
  std::vector<size_t> order;

  void reduce_one(size_t id) noexcept;
  void reduce_two(size_t id);
  bool conservatively_allocatable(size_t id) const noexcept;
  size_t select_rn() const noexcept;
  bool assign(size_t id) noexcept;

public:
  explicit ReductionSearch(const pbqp_problem &p) : problem(p), graph(p) {}
  void prepare();
  pbqp_status run(pbqp_error *error) noexcept;
  pbqp_solution *release_solution() noexcept { return solution.release(); }
};

void ReductionSearch::reduce_one(size_t id) noexcept {
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
    neighbor_costs[option] =
        add_reduction_costs(neighbor_costs[option], minimum);
  }
}

void ReductionSearch::reduce_two(size_t id) {
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
  graph.merge_edge(src, tgt, std::move(delta));
}

bool ReductionSearch::conservatively_allocatable(size_t id) const noexcept {
  const auto &node = graph.node(id);
  size_t available = 0;
  // Option zero is the spill preference, as in LLVM's register-allocation RN.
  // The solver still accepts arbitrary domains, including singleton nodes.
  for (size_t option = 1; option < node.costs.size(); ++option)
    available += node.costs[option].feasible;
  if (!available)
    return false;

  size_t denied = 0;
  for (size_t edge : node.edges) {
    const auto &neighbor = graph.node(graph.other(edge, id));
    size_t worst = 0;
    for (size_t other = 0; other < neighbor.costs.size(); ++other) {
      if (!neighbor.costs[other].feasible)
        continue;
      size_t blocked = 0;
      for (size_t option = 1; option < node.costs.size(); ++option)
        if (node.costs[option].feasible &&
            !graph.cost(edge, id, option, other).feasible)
          ++blocked;
      worst = std::max(worst, blocked);
    }
    denied += std::min(worst, available - denied);
  }
  if (denied < available)
    return true;

  // A universally compatible option can survive even when the conservative
  // sum double-counts conflicts against other options.
  for (size_t option = 1; option < node.costs.size(); ++option) {
    if (!node.costs[option].feasible)
      continue;
    bool safe = true;
    for (size_t edge : node.edges) {
      const auto &neighbor = graph.node(graph.other(edge, id));
      for (size_t other = 0; other < neighbor.costs.size(); ++other)
        if (neighbor.costs[other].feasible &&
            !graph.cost(edge, id, option, other).feasible) {
          safe = false;
          break;
        }
      if (!safe)
        break;
    }
    if (safe)
      return true;
  }
  return false;
}

size_t ReductionSearch::select_rn() const noexcept {
  size_t selected = graph.num_nodes();
  for (size_t id = 0; id < graph.num_nodes(); ++id) {
    if (!graph.node(id).active)
      continue;
    // Conservatively allocatable nodes take precedence, with ID tie-breaking.
    if (conservatively_allocatable(id))
      return id;
    if (selected == graph.num_nodes()) {
      selected = id;
      continue;
    }
    auto cost = graph.node(id).costs[0];
    auto best = graph.node(selected).costs[0];
    if (better_reduction_cost(cost, best) ||
        (!better_reduction_cost(best, cost) &&
         graph.degree(id) < graph.degree(selected)))
      selected = id;
  }
  return selected;
}

void ReductionSearch::prepare() {
  solution = make_solution(problem.num_nodes);
  order.reserve(problem.num_nodes);
  std::set<size_t> low_degree;
  for (size_t id = 0; id < graph.num_nodes(); ++id)
    if (graph.degree(id) <= 2)
      low_degree.insert(id);
  while (order.size() < graph.num_nodes()) {
    size_t id;
    if (low_degree.empty()) {
      id = select_rn();
    } else {
      auto first = low_degree.begin();
      id = *first;
      low_degree.erase(first);
    }
    switch (graph.degree(id)) {
    case 1:
      reduce_one(id);
      break;
    case 2:
      reduce_two(id);
      break;
    default:
      // R0 and RN preserve costs for reverse assignment without propagation.
      break;
    }
    graph.disconnect(id);
    order.push_back(id);
    for (size_t edge : graph.node(id).edges) {
      size_t neighbor = graph.other(edge, id);
      if (graph.degree(neighbor) <= 2)
        low_degree.insert(neighbor);
    }
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
      return pbqp_fail(error, PBQP_NO_SOLUTION, 0,
                       "reduction backpropagation reached a dead end; "
                       "infeasibility is not proven");
  if (!evaluate_assignment(problem, solution->options, solution->cost))
    return pbqp_fail(
        error, PBQP_NO_SOLUTION, 0,
        "reduction assignment is forbidden; infeasibility is not proven");
  solution->quality = PBQP_SOLUTION_FEASIBLE;
  return PBQP_OK;
}
