/*
 * Exact lexicographic branch-and-bound search.
 *
 * Seeds an upper bound with greedy search and caches unary, matrix, and row
 * minima before iterative depth-first search.
 * Bounds retain reference-order floating-point addition rather than regrouping
 * suffix sums. Search and progress reporting allocate no working storage.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <limits>
#include <vector>

module pbqp:branch_and_bound;
import :error;
import :evaluation;
import :greedy;
import :ownership;
import :progress;

class BranchAndBoundSearch {
  struct EdgeMinimum {
    size_t row_offset = 0;
    pbqp_cost_t minimum = PBQP_INF;
  };

  const pbqp_problem &problem;
  pbqp_progress_callback progress;
  void *user_data;
  SolutionPtr solution;
  pbqp_cost_t upper_bound = PBQP_INF;
  std::vector<size_t> tuple;
  std::vector<size_t> boundary;
  std::vector<pbqp_cost_t> unary_minima;
  std::vector<EdgeMinimum> edge_minima;
  std::vector<pbqp_cost_t> row_minima;

  pbqp_status allocate_workspace(pbqp_error *error);
  void compute_minima() noexcept;
  void seed_upper_bound();
  bool lower_bound(size_t depth, pbqp_cost_t &cost) const noexcept;
  bool advance(size_t &depth) noexcept;
  void keep_assignment(pbqp_cost_t cost) noexcept;
  template <class Observer> bool search(Observer observe) noexcept;

public:
  BranchAndBoundSearch(const pbqp_problem &p, pbqp_progress_callback callback,
                       void *data) noexcept
      : problem(p), progress(callback), user_data(data) {}
  pbqp_status prepare(pbqp_error *error);
  pbqp_status run(pbqp_error *error) noexcept;
  pbqp_solution *release_solution() noexcept { return solution.release(); }
};

pbqp_status BranchAndBoundSearch::allocate_workspace(pbqp_error *error) {
  const auto limit = std::numeric_limits<size_t>::max();
  size_t rows = 0;
  for (size_t k = 0; k < problem.num_edges; ++k) {
    size_t count = problem.nodes[problem.edges[k].src].num_options;
    if (count > limit - rows)
      return pbqp_fail(error, PBQP_OVERFLOW, 0, "row minimum count overflow");
    rows += count;
  }
  solution = make_solution(problem.num_nodes);
  tuple.assign(problem.num_nodes, 0);
  unary_minima.resize(problem.num_nodes);
  if (progress)
    boundary.resize(problem.num_nodes);
  edge_minima.assign(problem.num_edges, EdgeMinimum{});
  row_minima.resize(rows);
  return PBQP_OK;
}

void BranchAndBoundSearch::compute_minima() noexcept {
  for (size_t node = 0; node < problem.num_nodes; ++node) {
    const auto &unary = problem.nodes[node];
    unary_minima[node] =
        *std::min_element(unary.costs, unary.costs + unary.num_options);
  }
  size_t offset = 0;
  for (size_t k = 0; k < problem.num_edges; ++k) {
    const auto &edge = problem.edges[k];
    auto &bounds = edge_minima[k];
    size_t rows = problem.nodes[edge.src].num_options;
    size_t cols = problem.nodes[edge.tgt].num_options;
    bounds.row_offset = offset;
    for (size_t row = 0; row < rows; ++row) {
      const auto *begin = edge.costs + row * cols;
      auto minimum = *std::min_element(begin, begin + cols);
      row_minima[offset + row] = minimum;
      bounds.minimum = std::min(bounds.minimum, minimum);
    }
    offset += rows;
  }
}

void BranchAndBoundSearch::seed_upper_bound() {
  GreedySearch greedy(problem);
  // A heuristic dead end supplies no bound and is not an infeasibility proof.
  if (greedy.prepare(nullptr) == PBQP_OK && greedy.run(nullptr) == PBQP_OK)
    upper_bound = greedy.solution_cost();
}

pbqp_status BranchAndBoundSearch::prepare(pbqp_error *error) {
  auto status = allocate_workspace(error);
  if (status != PBQP_OK)
    return status;
  compute_minima();
  seed_upper_bound();
  return PBQP_OK;
}

bool BranchAndBoundSearch::lower_bound(size_t depth,
                                       pbqp_cost_t &cost) const noexcept {
  cost = 0;
  for (size_t node = 0; node < problem.num_nodes; ++node) {
    auto term = node < depth ? problem.nodes[node].costs[tuple[node]]
                             : unary_minima[node];
    if (term == PBQP_INF)
      return false;
    cost = pbqp_cost_add(cost, term);
  }
  for (size_t k = 0; k < problem.num_edges; ++k) {
    const auto &edge = problem.edges[k];
    pbqp_cost_t term;
    if (edge.tgt < depth)
      term = edge.costs[tuple[edge.src] * problem.nodes[edge.tgt].num_options +
                        tuple[edge.tgt]];
    else if (edge.src < depth)
      term = row_minima[edge_minima[k].row_offset + tuple[edge.src]];
    else
      term = edge_minima[k].minimum;
    if (term == PBQP_INF)
      return false;
    // Every term is a lower bound on the corresponding finite reference term.
    // Rounded addition is monotone: keeping the exact term order preserves a
    // lower bound even with negative costs or infinite accumulated sums.
    cost = pbqp_cost_add(cost, term);
  }
  return true;
}

bool BranchAndBoundSearch::advance(size_t &depth) noexcept {
  while (depth) {
    size_t node = depth - 1;
    if (++tuple[node] < problem.nodes[node].num_options)
      return true;
    --depth;
  }
  return false;
}

void BranchAndBoundSearch::keep_assignment(pbqp_cost_t cost) noexcept {
  solution->cost = cost;
  upper_bound = cost;
  if (problem.num_nodes)
    std::copy_n(tuple.data(), problem.num_nodes, solution->options);
}

template <class Observer>
bool BranchAndBoundSearch::search(Observer observe) noexcept {
  bool found = false;
  size_t depth = 0;
  for (;;) {
    pbqp_cost_t bound;
    // A greedy seed may be later than the first tied optimum. Once search
    // finds a solution, subsequent ties cannot precede it and can be pruned.
    if (lower_bound(depth, bound) &&
        (found ? bound < upper_bound : bound <= upper_bound)) {
      if (depth < problem.num_nodes) {
        tuple[depth++] = 0;
        continue;
      }
      keep_assignment(bound);
      found = true;
    }
    // The current prefix is fully searched, forbidden, or cannot win.
    observe(tuple.data(), depth);
    if (!advance(depth))
      break;
  }
  return found;
}

pbqp_status BranchAndBoundSearch::run(pbqp_error *error) noexcept {
  bool found;
  if (progress) {
    PercentProgress reporter(problem, progress, user_data, boundary.data());
    found = search([&](const size_t *prefix, size_t depth) noexcept {
      reporter.completed_prefix(prefix, depth);
    });
  } else {
    found = search([](const size_t *, size_t) noexcept {});
  }
  if (!found)
    return pbqp_fail(error, PBQP_INFEASIBLE, 0, "no feasible tuple");
  if (!evaluate_assignment(problem, solution->options, solution->cost))
    return pbqp_fail(error, PBQP_INVALID, 0,
                     "branch-and-bound produced a forbidden assignment");
  solution->quality = PBQP_SOLUTION_OPTIMAL;
  return PBQP_OK;
}
