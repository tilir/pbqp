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
#include <numeric>
#include <span>
#include <vector>

module pbqp:greedy;
import :error;
import :evaluation;
import :ownership;

namespace pbqp {
class GreedySearch {
  struct NodePriority {
    size_t id;
    pbqp_cost_t priority;
  };

  const pbqp_problem &problem;
  SolutionPtr solution;
  std::vector<NodePriority> order;
  std::vector<size_t> rank;
  std::vector<size_t> incident;

  static bool unary_cost_range(const pbqp_node &node,
                               pbqp_cost_t &range) noexcept;
  pbqp_status order_nodes(pbqp_error *error) noexcept;
  void index_edges() noexcept;
  size_t later_step(size_t edge) const noexcept;
  bool score_option(size_t node, size_t option, std::span<const size_t> edges,
                    pbqp_cost_t &score) const noexcept;
  bool assign_node(size_t node, std::span<const size_t> edges) noexcept;

public:
  explicit GreedySearch(const pbqp_problem &p) noexcept : problem(p) {}
  pbqp_status prepare(pbqp_error *error);
  pbqp_status run(pbqp_error *error) noexcept;
  pbqp_cost_t solution_cost() const noexcept { return solution->cost; }
  const pbqp_solution &get_solution() const noexcept { return *solution; }
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

pbqp_status GreedySearch::order_nodes(pbqp_error *error) noexcept {
  if (!problem.num_nodes)
    return PBQP_OK;
  for (size_t node = 0; node < problem.num_nodes; ++node) {
    pbqp_cost_t priority;
    if (!unary_cost_range(problem.nodes[node], priority))
      return set_error(error, PBQP_NO_SOLUTION, 0,
                       "greedy search found no allowed unary option; "
                       "infeasibility is not proven");
    order[node] = {node, priority};
  }
  std::sort(order.begin(), order.end(),
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

void GreedySearch::index_edges() noexcept {
  std::iota(incident.begin(), incident.end(), size_t{0});
  // Each edge contributes at its later endpoint's assignment step. Ties keep
  // original edge order so local floating-point sums remain unchanged.
  std::sort(incident.begin(), incident.end(),
            [this](size_t a, size_t b) noexcept {
              if (later_step(a) != later_step(b))
                return later_step(a) < later_step(b);
              return a < b;
            });
}

pbqp_status GreedySearch::prepare(pbqp_error *error) {
  solution = make_solution(problem.num_nodes);
  order.resize(problem.num_nodes);
  rank.resize(problem.num_nodes);
  incident.resize(problem.num_edges);
  auto status = order_nodes(error);
  if (status != PBQP_OK)
    return status;
  index_edges();
  return PBQP_OK;
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
    auto edges = std::span<const size_t>(incident).subspan(begin, end - begin);
    if (!assign_node(node, edges))
      return set_error(
          error, PBQP_NO_SOLUTION, 0,
          "greedy search reached a dead end; infeasibility is not proven");
  }
  if (!evaluate_assignment(problem, solution->options, solution->cost))
    return set_error(error, PBQP_NO_SOLUTION, 0,
                     "greedy assignment is forbidden");
  solution->quality = PBQP_SOLUTION_FEASIBLE;
  return PBQP_OK;
}
} // namespace pbqp
