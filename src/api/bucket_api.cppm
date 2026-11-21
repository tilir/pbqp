/*
 * Bucket elimination dispatch, bounded preparation, and numerical warnings.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <memory>
module pbqp:bucket_api;
import :bucket;
import :bucket_plan;
import :parallel;
import :solve_options;
import :deadline;
import :problem;
import :ownership;
import :error;

namespace pbqp {
pbqp_status solve_bucket(const pbqp_problem *p,
                         const pbqp_solve_options *options, pbqp_solution **out,
                         pbqp_error *error) {
  if (!out)
    return set_error(error, PBQP_INVALID, 0, "missing output pointer");
  const CommonSolveOptions config(options);
  const auto deadline = config.deadline();
  const auto budget =
      config.view.size("bucket-max-entries", bucket_default_entries);
  validate(p);
  if (const auto status = config.check_unknown(error); status != PBQP_OK)
    return status;
  if (deadline.expired())
    return set_error(error, PBQP_TIMEOUT, 0, "exact search timed out");
  const auto plan = BucketPlanner(*p).build(budget, deadline);
  if (deadline.expired())
    return set_error(error, PBQP_TIMEOUT, 0, "exact search timed out");
  if (plan.limit_reason) {
    config.log.line("bucket: rejected node=", plan.rejected_node,
                    "; separator arity=", plan.rejected_arity,
                    "; used entries=", plan.cells, "; entry budget=", budget);
    if (plan.rejected_cells)
      config.log.line("bucket: rejected factor entries=", plan.rejected_cells);
    else
      config.log.line("bucket: rejected factor entry count overflows size_t");
    if (const auto status = config.unsupported(plan.limit_reason, error);
        status != PBQP_OK)
      return status;
    return set_error(error, PBQP_NO_SOLUTION, 0, plan.limit_reason);
  }
  const bool exact = BucketArithmetic{}.exact(*p, deadline);
  if (deadline.expired())
    return set_error(error, PBQP_TIMEOUT, 0, "exact search timed out");
  if (!exact) {
    if (const auto status = config.unsupported(
            "bucket: regrouped costs may change the reference float optimum; "
            "solution quality is FEASIBLE",
            error);
        status != PBQP_OK)
      return status;
  }

  const auto threads = parallel_concurrency(config.threads);
  BucketSearch search(*p, plan, deadline, threads);
  size_t workers = 0;
  for (const auto &step : plan.steps)
    workers = std::max(workers, search.worker_count(step));
  config.log.line("bucket: induced width=", plan.width,
                  "; factor entries=", plan.cells, "; entry budget=", budget);
  config.log.line("search: prepared workers=", workers, " (peak per bucket)");
  SolutionPtr solution;
  const auto status =
      search.run(exact, config.progress, config.progress_data, solution);
  switch (status) {
  case PBQP_OK:
    *out = solution.release();
    return PBQP_OK;
  case PBQP_TIMEOUT:
    return set_error(error, status, 0, "exact search timed out");
  case PBQP_INFEASIBLE:
    return set_error(error, status, 0, "no feasible tuple");
  case PBQP_INTERNAL_ERROR:
    return set_error(error, status, 0, "bucket reconstruction is infeasible");
  default:
    return status;
  }
}
} // namespace pbqp
