/*
 * Greedy search state and cost-priority assignment.
 *
 * Separates workspace preparation, node priorities, edge indexing, and local
 * option scoring. Search performs no allocation, backtracking, or exception
 * handling; reported costs use the shared reference evaluation order.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;

#include "pbqp.h"
#include <algorithm>
#include <limits>
#include <memory>
#include <new>
#include <numeric>
#include <span>

module pbqp:greedy;
import :error;
import :evaluation;
import :ownership;

class GreedySearch {
  struct NodePriority {
    size_t id;
    pbqp_cost_t priority;
  };

  const pbqp_problem &problem;
  SolutionPtr solution{nullptr, solution_deleter};
  std::unique_ptr<NodePriority[]> order;
  std::unique_ptr<size_t[]> rank;
  std::unique_ptr<size_t[]> incident;

  static bool unary_cost_range(const pbqp_node &node,
                               pbqp_cost_t &range) noexcept;
  pbqp_status allocate_solution(pbqp_error *error) noexcept;
  pbqp_status order_nodes(pbqp_error *error) noexcept;
  pbqp_status index_edges(pbqp_error *error) noexcept;
  size_t later_step(size_t edge) const noexcept;
  bool score_option(size_t node, size_t option, std::span<const size_t> edges,
                    pbqp_cost_t &score) const noexcept;
  bool assign_node(size_t node, std::span<const size_t> edges) noexcept;

public:
  explicit GreedySearch(const pbqp_problem &p) noexcept : problem(p) {}
  pbqp_status prepare(pbqp_error *error) noexcept;
  pbqp_status run(pbqp_error *error) noexcept;
  pbqp_solution *release_solution() noexcept { return solution.release(); }
};

bool GreedySearch::unary_cost_range(const pbqp_node &node,
                                    pbqp_cost_t &range) noexcept {
  bool found = false;
  pbqp_cost_t minimum = 0, maximum = 0;
  for (size_t option = 0; option < node.num_options; ++option) {
    auto cost = node.costs[option];
    if (cost == PBQP_INF)
      continue;
    if (!found) {
      minimum = maximum = cost;
      found = true;
    } else {
      minimum = std::min(minimum, cost);
      maximum = std::max(maximum, cost);
    }
  }
  range = maximum - minimum;
  return found;
}

pbqp_status GreedySearch::allocate_solution(pbqp_error *error) noexcept {
  solution.reset(new (std::nothrow) pbqp_solution{});
  if (!solution)
    return pbqp_fail(error, PBQP_NO_MEMORY, 0, "allocation failed");
  solution->num_nodes = problem.num_nodes;
  if (problem.num_nodes) {
    solution->options = new (std::nothrow) size_t[problem.num_nodes];
    if (!solution->options)
      return pbqp_fail(error, PBQP_NO_MEMORY, 0, "allocation failed");
  }
  return PBQP_OK;
}

pbqp_status GreedySearch::order_nodes(pbqp_error *error) noexcept {
  if (!problem.num_nodes)
    return PBQP_OK;
  order.reset(new (std::nothrow) NodePriority[problem.num_nodes]);
  rank.reset(new (std::nothrow) size_t[problem.num_nodes]);
  if (!order || !rank)
    return pbqp_fail(error, PBQP_NO_MEMORY, 0, "allocation failed");
  for (size_t node = 0; node < problem.num_nodes; ++node) {
    pbqp_cost_t priority;
    if (!unary_cost_range(problem.nodes[node], priority))
      return pbqp_fail(error, PBQP_NO_SOLUTION, 0,
                       "greedy search found no allowed unary option; "
                       "infeasibility is not proven");
    order[node] = {node, priority};
  }
  std::sort(order.get(), order.get() + problem.num_nodes,
            [](const NodePriority &a, const NodePriority &b) noexcept {
              if (a.priority != b.priority)
                return a.priority > b.priority;
              return a.id < b.id;
            });
  for (size_t step = 0; step < problem.num_nodes; ++step)
    rank[order[step].id] = step;
  return PBQP_OK;
}

size_t GreedySearch::later_step(size_t edge) const noexcept {
  return std::max(rank[problem.edges[edge].src], rank[problem.edges[edge].tgt]);
}

pbqp_status GreedySearch::index_edges(pbqp_error *error) noexcept {
  if (!problem.num_edges)
    return PBQP_OK;
  incident.reset(new (std::nothrow) size_t[problem.num_edges]);
  if (!incident)
    return pbqp_fail(error, PBQP_NO_MEMORY, 0, "allocation failed");
  std::iota(incident.get(), incident.get() + problem.num_edges, size_t{0});
  // Each edge contributes at its later endpoint's assignment step. Ties keep
  // original edge order so local floating-point sums remain unchanged.
  std::sort(incident.get(), incident.get() + problem.num_edges,
            [this](size_t a, size_t b) noexcept {
              if (later_step(a) != later_step(b))
                return later_step(a) < later_step(b);
              return a < b;
            });
  return PBQP_OK;
}

pbqp_status GreedySearch::prepare(pbqp_error *error) noexcept {
  if (problem.num_nodes >
          std::numeric_limits<size_t>::max() / sizeof(NodePriority) ||
      problem.num_edges > std::numeric_limits<size_t>::max() / sizeof(size_t))
    return pbqp_fail(error, PBQP_OVERFLOW, 0,
                     "solver allocation size overflow");
  auto status = allocate_solution(error);
  if (status != PBQP_OK)
    return status;
  status = order_nodes(error);
  if (status != PBQP_OK)
    return status;
  return index_edges(error);
}

bool GreedySearch::score_option(size_t node, size_t option,
                                std::span<const size_t> edges,
                                pbqp_cost_t &score) const noexcept {
  score = problem.nodes[node].costs[option];
  if (score == PBQP_INF)
    return false;
  for (size_t index : edges) {
    const auto &edge = problem.edges[index];
    size_t row = edge.src == node ? option : solution->options[edge.src];
    size_t col = edge.tgt == node ? option : solution->options[edge.tgt];
    pbqp_cost_t pair =
        edge.costs[row * problem.nodes[edge.tgt].num_options + col];
    if (pair == PBQP_INF)
      return false;
    score = pbqp_cost_add(score, pair);
  }
  return true;
}

bool GreedySearch::assign_node(size_t node,
                               std::span<const size_t> edges) noexcept {
  bool found = false;
  pbqp_cost_t best = 0;
  for (size_t option = 0; option < problem.nodes[node].num_options; ++option) {
    pbqp_cost_t score;
    if (score_option(node, option, edges, score) && (!found || score < best)) {
      found = true;
      best = score;
      solution->options[node] = option;
    }
  }
  return found;
}

pbqp_status GreedySearch::run(pbqp_error *error) noexcept {
  size_t end = 0;
  for (size_t step = 0; step < problem.num_nodes; ++step) {
    size_t node = order[step].id;
    size_t begin = end;
    while (end < problem.num_edges && later_step(incident[end]) == step)
      ++end;
    auto edges = begin == end ? std::span<const size_t>{}
                              : std::span<const size_t>(incident.get() + begin,
                                                        end - begin);
    if (!assign_node(node, edges))
      return pbqp_fail(
          error, PBQP_NO_SOLUTION, 0,
          "greedy search reached a dead end; infeasibility is not proven");
  }
  if (!evaluate_assignment(problem, solution->options, solution->cost))
    return pbqp_fail(error, PBQP_NO_SOLUTION, 0,
                     "greedy assignment is forbidden");
  solution->quality = PBQP_SOLUTION_FEASIBLE;
  return PBQP_OK;
}
