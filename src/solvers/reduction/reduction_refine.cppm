/*
 * Bounded, incumbent-conditioned bucket refinement for the reduction heuristic.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <memory>
#include <vector>
module pbqp:reduction_refine;
import :bucket;
import :bucket_plan;
import :bucket_residual;
import :deadline;
import :diagnostics;
import :evaluation;
import :ownership;

namespace pbqp {
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
  BucketResidual residual(problem, seed);
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
