/*
 * Sparse induced graph, bounded factor layout, and float reassociation check.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <set>
#include <span>
#include <utility>
#include <vector>
module pbqp:bucket_plan;
import :deadline;

namespace pbqp {
constexpr size_t bucket_default_entries = 4 * 1024 * 1024;

struct BucketStep {
  size_t node;
  std::vector<size_t> scope;
  size_t cells;
};

struct BucketPlan {
  std::vector<BucketStep> steps;
  size_t width = 0;
  size_t cells = 0;
  const char *limit_reason = nullptr;
  size_t rejected_node = 0, rejected_arity = 0, rejected_cells = 0;
};

class BucketPlanner {
  const pbqp_problem &problem;
  std::vector<std::set<size_t>> neighbors;
  std::set<std::pair<size_t, size_t>> degrees;
  // Bound graph work separately: singleton domains can yield tiny tables
  // even when filling a separator would require many adjacency updates.
  size_t fill_checks = 0;
  static constexpr size_t fill_budget = 8 * 1024 * 1024;

  bool can_fill(size_t degree) noexcept {
    if (degree < 2)
      return true;
    const size_t a = degree % 2 ? degree : degree / 2;
    const size_t b = degree % 2 ? (degree - 1) / 2 : degree - 1;
    if (a > (fill_budget - fill_checks) / b)
      return false;
    fill_checks += a * b;
    return true;
  }

  void fill(const std::vector<size_t> &scope) {
    for (auto left = scope.begin(); left != scope.end(); ++left)
      for (auto right = left + 1; right != scope.end(); ++right)
        if (neighbors[*left].insert(*right).second)
          neighbors[*right].insert(*left);
  }

  size_t table_size(const std::vector<size_t> &scope) const {
    size_t cells = 1;
    for (size_t id : scope) {
      const auto domain = problem.nodes[id].num_options;
      if (cells > std::numeric_limits<size_t>::max() / domain)
        return 0;
      cells *= domain;
    }
    return cells;
  }

public:
  explicit BucketPlanner(const pbqp_problem &p)
      : problem(p), neighbors(p.num_nodes) {
    for (const auto &edge : std::span(p.edges, p.num_edges)) {
      neighbors[edge.src].insert(edge.tgt);
      neighbors[edge.tgt].insert(edge.src);
    }
    for (size_t id = 0; id < p.num_nodes; ++id)
      degrees.emplace(neighbors[id].size(), id);
  }

  BucketPlan build(size_t budget, Deadline deadline,
                   size_t max_width = std::numeric_limits<size_t>::max()) {
    BucketPlan plan;
    plan.steps.reserve(problem.num_nodes);
    while (!degrees.empty() && !deadline.expired()) {
      const size_t id = degrees.begin()->second;
      std::vector<size_t> scope(neighbors[id].begin(), neighbors[id].end());
      const size_t cells = table_size(scope);
      if (scope.size() > max_width)
        plan.limit_reason = "bucket: induced width exceeds the limit";
      else if (!cells || cells > budget - plan.cells)
        plan.limit_reason = "bucket: generated factor entry budget exceeded";
      else if (!can_fill(scope.size()))
        plan.limit_reason = "bucket: separator-pair check budget exceeded";
      if (plan.limit_reason) {
        plan.rejected_node = id;
        plan.rejected_arity = scope.size();
        plan.rejected_cells = cells;
        break;
      }
      plan.width = std::max(plan.width, scope.size());
      plan.cells += cells;
      degrees.erase(degrees.begin());
      for (size_t neighbor : scope) {
        degrees.erase({neighbors[neighbor].size(), neighbor});
        neighbors[neighbor].erase(id);
      }
      neighbors[id].clear();
      fill(scope);
      for (size_t neighbor : scope)
        degrees.emplace(neighbors[neighbor].size(), neighbor);
      plan.steps.push_back({id, std::move(scope), cells});
    }
    return plan;
  }
};

class BucketArithmetic {
  int unit = std::numeric_limits<int>::max();
  double absolute_sum = 0;

  bool add(const pbqp_cost_t *costs, size_t count, Deadline deadline);

public:
  bool exact(const pbqp_problem &problem, Deadline deadline);
};

bool BucketArithmetic::add(const pbqp_cost_t *costs, size_t count,
                           Deadline deadline) {
  constexpr int digits = std::numeric_limits<pbqp_cost_t>::digits;
  double maximum = 0;
  for (size_t i = 0; i < count; ++i) {
    if (i % 256 == 0 && deadline.expired())
      return false;
    const double magnitude = std::abs(static_cast<double>(costs[i]));
    if (!magnitude || !std::isfinite(magnitude))
      continue;
    int exp;
    static_assert(digits <= std::numeric_limits<std::uintmax_t>::digits);
    const auto significand = static_cast<std::uintmax_t>(
        std::ldexp(std::frexp(magnitude, &exp), digits));
    unit = std::min(unit, exp - digits + std::countr_zero(significand));
    maximum = std::max(maximum, magnitude);
  }
  if (!maximum)
    return true;
  // Check in units of the smallest power-of-two lattice. Below the cap each
  // sum of integer units is exact even in float. A newly discovered finer
  // unit can only increase the count. Check float range separately.
  const double previous = std::ldexp(absolute_sum, -unit);
  const double terms = std::ldexp(maximum, -unit);
  const double limit = std::ldexp(1.0, digits);
  if (previous > limit || terms > limit || previous + terms > limit)
    return false;
  absolute_sum = std::ldexp(previous + terms, unit);
  return absolute_sum <= std::numeric_limits<pbqp_cost_t>::max();
}

bool BucketArithmetic::exact(const pbqp_problem &p, Deadline deadline) {
  static_assert(std::numeric_limits<pbqp_cost_t>::radix == 2);
  for (const auto &node : std::span(p.nodes, p.num_nodes))
    if (!add(node.costs, node.num_options, deadline))
      return false;
  for (const auto &edge : std::span(p.edges, p.num_edges))
    if (!add(edge.costs,
             p.nodes[edge.src].num_options * p.nodes[edge.tgt].num_options,
             deadline))
      return false;
  return true;
}
} // namespace pbqp
