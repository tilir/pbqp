/*
 * Bounded, incumbent-conditioned bucket refinement for the reduction heuristic.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <span>
#include <vector>
module pbqp:reduction_refine;
import :bucket;
import :bucket_plan;
import :deadline;
import :diagnostics;
import :evaluation;
import :ownership;

namespace pbqp {
class ReductionResidual {
  const pbqp_problem &problem;
  const pbqp_solution &seed;
  std::vector<size_t> ids, local, degree;
  std::vector<bool> fixed;
  std::vector<pbqp_node> nodes;
  std::vector<pbqp_edge> edges;
  std::vector<std::vector<pbqp_cost_t>> unaries;

  bool apply_boundary(const pbqp_edge &edge);

public:
  ReductionResidual(const pbqp_problem &p, const pbqp_solution &solution)
      : problem(p), seed(solution), local(p.num_nodes), fixed(p.num_nodes) {}

  pbqp_problem rebuild();
  bool fix_high_degree();
  bool condition();
  void merge(const pbqp_solution &residual, pbqp_solution &result) const;
};

pbqp_problem ReductionResidual::rebuild() {
  ids.clear();
  nodes.clear();
  edges.clear();
  for (size_t id = 0; id < problem.num_nodes; ++id) {
    if (fixed[id])
      continue;
    local[id] = ids.size();
    ids.push_back(id);
    nodes.push_back(problem.nodes[id]);
  }
  degree.assign(ids.size(), 0);
  for (const auto &edge : std::span(problem.edges, problem.num_edges)) {
    if (fixed[edge.src] || fixed[edge.tgt])
      continue;
    const size_t src = local[edge.src], tgt = local[edge.tgt];
    edges.push_back({src, tgt, edge.costs});
    ++degree[src];
    ++degree[tgt];
  }
  return {nodes.size(), edges.size(), nodes.data(), edges.data()};
}

bool ReductionResidual::fix_high_degree() {
  const auto highest = std::ranges::max_element(degree);
  if (highest == degree.end() || !*highest)
    return false;
  fixed[ids[highest - degree.begin()]] = true;
  return true;
}

bool ReductionResidual::apply_boundary(const pbqp_edge &edge) {
  const bool src_fixed = fixed[edge.src];
  const size_t other = src_fixed ? edge.tgt : edge.src;
  const size_t id = local[other];
  const size_t cols = problem.nodes[edge.tgt].num_options;
  auto &costs = unaries[id];
  if (costs.empty()) {
    const auto &node = nodes[id];
    costs.assign(node.costs, node.costs + node.num_options);
    nodes[id].costs = costs.data();
  }
  const size_t row = src_fixed ? seed.options[edge.src] * cols : 0;
  const size_t column = src_fixed ? 0 : seed.options[edge.tgt];
  const size_t stride = src_fixed ? 1 : cols;
  for (size_t option = 0; option < costs.size(); ++option) {
    const auto cost = edge.costs[row + column + option * stride];
    if (costs[option] == PBQP_INF || cost == PBQP_INF) {
      costs[option] = PBQP_INF;
      continue;
    }
    const auto sum = pbqp_cost_add(costs[option], cost);
    if (!std::isfinite(sum))
      return false;
    costs[option] = sum;
  }
  return true;
}

bool ReductionResidual::condition() {
  unaries.resize(nodes.size());
  for (const auto &edge : std::span(problem.edges, problem.num_edges))
    if (fixed[edge.src] != fixed[edge.tgt] && !apply_boundary(edge))
      return false;
  return true;
}

void ReductionResidual::merge(const pbqp_solution &residual,
                              pbqp_solution &result) const {
  std::copy_n(seed.options, seed.num_nodes, result.options);
  for (size_t i = 0; i < ids.size(); ++i)
    result.options[ids[i]] = residual.options[i];
}

bool refinement_work_fits(const pbqp_problem &problem,
                          const BucketPlan &plan) noexcept {
  size_t remaining = 8 * 1024 * 1024;
  for (const auto &step : plan.steps) {
    const auto domain = problem.nodes[step.node].num_options;
    if (step.cells > remaining / domain)
      return false;
    remaining -= step.cells * domain;
  }
  return true;
}

SolutionPtr refine_reduction(const pbqp_problem &problem,
                             const pbqp_solution &seed, size_t width,
                             Diagnostics log) {
  constexpr size_t max_fixed = 32;
  ReductionResidual residual(problem, seed);
  for (size_t fixed = 0; fixed <= max_fixed; ++fixed) {
    const auto view = residual.rebuild();
    const auto plan =
        BucketPlanner(view).build(bucket_default_entries, Deadline{}, width);
    if (plan.limit_reason || !refinement_work_fits(view, plan)) {
      if (fixed == max_fixed || !residual.fix_high_degree())
        break;
      continue;
    }
    log.line("r1r2rn: bucket refinement fixed=", fixed,
             "; induced width=", plan.width, "; factor entries=", plan.cells);
    if (!view.num_nodes)
      return {};
    if (!residual.condition()) {
      log.line("r1r2rn: bucket refinement skipped: conditioned unary overflow");
      return {};
    }
    BucketSearch search(view, plan, Deadline{}, 1);
    SolutionPtr candidate;
    if (search.run(false, nullptr, nullptr, candidate) != PBQP_OK) {
      log.line("r1r2rn: bucket refinement could not reconstruct a candidate");
      return {};
    }
    auto merged = make_solution(problem.num_nodes);
    residual.merge(*candidate, *merged);
    if (!evaluate_assignment(problem, merged->options, merged->cost) ||
        !(merged->cost < seed.cost)) {
      log.line("r1r2rn: bucket refinement retained cost=", seed.cost);
      return {};
    }
    merged->quality = PBQP_SOLUTION_FEASIBLE;
    log.line("r1r2rn: bucket refinement improved cost from ", seed.cost, " to ",
             merged->cost);
    return merged;
  }
  log.line("r1r2rn: bucket refinement skipped: no residual within limits");
  return {};
}
} // namespace pbqp
