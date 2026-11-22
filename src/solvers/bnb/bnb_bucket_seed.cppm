/*
 * Bounded neighborhood search around a feasible B&B incumbent.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <array>
#include <memory>
#include <span>
#include <vector>
module pbqp:bnb_bucket_seed;
import :bnb_incumbent;
import :bucket;
import :bucket_plan;
import :bucket_residual;
import :deadline;
import :diagnostics;
import :evaluation;
import :ownership;
import :interference;

namespace pbqp {
class BnbBucketSeed {
  static constexpr size_t max_group = 4;
  static constexpr size_t max_attempts = 96;
  static constexpr size_t max_rounds = 4;
  static constexpr size_t candidate_budget = 2 * 1024 * 1024;
  static constexpr size_t table_budget = 65536;
  static constexpr size_t visit_budget = 4 * 1024 * 1024;
  static constexpr size_t domain_limit = 256;
  const pbqp_problem &problem;
  BranchAndBoundIncumbent &incumbent;
  Deadline deadline;
  pbqp_cost_t lower;
  BucketResidual residual;
  SolutionPtr merged;
  size_t attempts = 0, improvements = 0, remaining = candidate_budget;
  size_t visits_left = visit_budget;
  bool visit_limit = false;

  bool active() const noexcept {
    return attempts < max_attempts && remaining && !visit_limit &&
           !deadline.expired() && incumbent.can_improve(lower);
  }

  bool charge(const pbqp_problem &view, const BucketPlan &plan) noexcept;
  void optimize(std::span<const size_t> group);
  void interference_round(const std::vector<std::vector<size_t>> &neighbors);
  void generic_round();

public:
  BnbBucketSeed(const pbqp_problem &p, BranchAndBoundIncumbent &best,
                pbqp_cost_t bound, Deadline limit)
      : problem(p), incumbent(best), deadline(limit), lower(bound),
        residual(p, best.result()), merged(make_solution(p.num_nodes)) {}

  void run(bool interference, Diagnostics log);
};

bool BnbBucketSeed::charge(const pbqp_problem &view,
                           const BucketPlan &plan) noexcept {
  size_t budget = remaining;
  for (const auto &step : plan.steps) {
    const size_t domain = view.nodes[step.node].num_options;
    if (step.cells > budget / domain)
      return false;
    budget -= step.cells * domain;
  }
  remaining = budget;
  return true;
}

void BnbBucketSeed::optimize(std::span<const size_t> group) {
  ++attempts;
  if (std::ranges::any_of(group, [&](size_t id) {
        return problem.nodes[id].num_options > domain_limit;
      }))
    return;
  // Rebuilding and conditioning each scan the original edge list once.
  if (problem.num_nodes > visits_left ||
      problem.num_edges > (visits_left - problem.num_nodes) / 2) {
    visit_limit = true;
    return;
  }
  visits_left -= problem.num_nodes + 2 * problem.num_edges;
  residual.restrict_to(group);
  const auto view = residual.rebuild();
  if (!residual.condition(deadline) || !residual.compact(deadline))
    return;
  const auto plan = BucketPlanner(view).build(table_budget, deadline);
  if (deadline.expired() || plan.limit_reason || !charge(view, plan))
    return;
  BucketSearch search(view, plan, deadline, 1);
  SolutionPtr candidate;
  if (search.run(false, nullptr, nullptr, candidate) != PBQP_OK)
    return;
  residual.merge(*candidate, *merged);
  if (evaluate_assignment(problem, merged->options, merged->cost) &&
      incumbent.can_improve(merged->cost)) {
    incumbent.update(merged->options, merged->cost);
    ++improvements;
  }
}

void BnbBucketSeed::interference_round(
    const std::vector<std::vector<size_t>> &neighbors) {
  const auto &seed = incumbent.result();
  std::vector<size_t> spills;
  for (size_t id = 0; id < problem.num_nodes; ++id)
    if (!seed.options[id] && problem.nodes[id].costs[0] > 0)
      spills.push_back(id);
  std::ranges::stable_sort(spills, [&](size_t a, size_t b) {
    return problem.nodes[a].costs[0] > problem.nodes[b].costs[0];
  });
  const size_t domain = problem.nodes[0].num_options;

  struct Group {
    std::array<size_t, max_group> ids{};
    size_t size = 1;
  };

  std::vector<Group> groups(domain);
  for (size_t id : spills) {
    if (!active())
      break;
    if (seed.options[id])
      continue;
    std::fill(groups.begin(), groups.end(), Group{{id}, 1});
    for (size_t neighbor : neighbors[id]) {
      const size_t color = seed.options[neighbor];
      if (!color)
        continue;
      auto &group = groups[color];
      if (group.size < max_group)
        group.ids[group.size] = neighbor;
      group.size = std::min(group.size + 1, max_group + 1);
    }
    // Prefer fewer blockers and try each color against the same incumbent.
    // If an improvement changes the coloring, rebuild groups on the next pass.
    const size_t before = improvements;
    bool singleton_tried = false;
    for (size_t size = 1; size <= max_group && active(); ++size)
      for (size_t color = 1; color < domain && active(); ++color) {
        const auto &g = groups[color];
        if (g.size != size || (size == 1 && singleton_tried))
          continue;
        singleton_tried |= size == 1;
        optimize(std::span(g.ids.data(), g.size));
        if (improvements != before)
          return;
      }
  }
}

void BnbBucketSeed::generic_round() {
  const auto &seed = incumbent.result();
  std::vector<size_t> order(problem.num_edges);
  for (size_t i = 0; i < order.size(); ++i)
    order[i] = i;
  const auto local_cost = [&](size_t i) {
    const auto &e = problem.edges[i];
    const auto &a = problem.nodes[e.src];
    const auto &b = problem.nodes[e.tgt];
    const size_t x = seed.options[e.src], y = seed.options[e.tgt];
    return static_cast<double>(a.costs[x]) + b.costs[y] +
           e.costs[x * b.num_options + y];
  };
  std::ranges::stable_sort(
      order, [&](size_t a, size_t b) { return local_cost(a) > local_cost(b); });
  for (size_t i : order) {
    if (!active())
      break;
    const auto &edge = problem.edges[i];
    const std::array group{edge.src, edge.tgt};
    optimize(group);
  }
}

void BnbBucketSeed::run(bool interference, Diagnostics log) {
  if (problem.num_nodes > visit_budget ||
      problem.num_edges > (visit_budget - problem.num_nodes) / 2) {
    log.line("bnb: bucket neighborhood seed skipped: problem scan budget");
    return;
  }
  if (interference && problem.nodes[0].num_options > domain_limit) {
    log.line("bnb: bucket neighborhood seed skipped: domain exceeds ",
             domain_limit, " options");
    return;
  }
  const auto start = incumbent.result().cost;
  std::vector<std::vector<size_t>> neighbors;
  if (interference) {
    neighbors.resize(problem.num_nodes);
    for (const auto &edge : std::span(problem.edges, problem.num_edges)) {
      if (deadline.expired())
        return;
      neighbors[edge.src].push_back(edge.tgt);
      neighbors[edge.tgt].push_back(edge.src);
    }
  }
  for (size_t round = 0; round < max_rounds && active(); ++round) {
    const size_t before = improvements;
    if (interference)
      interference_round(neighbors);
    else
      generic_round();
    if (before == improvements)
      break;
  }
  if (visit_limit)
    log.line("bnb: bucket neighborhood seed: problem scan budget reached");
  log.line("bnb: bucket neighborhood seed: attempts=", attempts,
           "; improvements=", improvements, "; cost=", start, " -> ",
           incumbent.result().cost);
}
} // namespace pbqp
