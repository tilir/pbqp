/*
 * Internal reduction API orchestration: option checks, validated borrowed
 * input, owned search preparation, and result transfer. Exceptions are
 * translated only by the thin public C entry point.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <memory>
#include <string_view>
module pbqp:reduction_api;
import :solve_options;
import :error;
import :diagnostics;
import :problem;
import :reduction;
import :reduction_refine;
import :ownership;

namespace pbqp {
pbqp_status solve_reduction(const pbqp_problem *problem,
                            const pbqp_solve_options *options,
                            pbqp_solution **out, pbqp_error *error) {
  if (!out)
    return set_error(error, PBQP_INVALID, 0, "missing output pointer");
  const CommonSolveOptions config(options);
  const auto width = config.view.size("bucket-width", 2);
  validate(problem);
  if (auto status = config.check_heuristic(error); status != PBQP_OK)
    return status;
  const auto log = config.log;
  log.line("r1r2rn: preparing single-thread heuristic search");
  ReductionSearch search(*problem);
  search.prepare();
  auto status = search.run(error);
  if (status != PBQP_OK)
    return status;
  if (width > 2) {
    log.line("r1r2rn: bucket refinement width limit=", width);
    if (auto refined =
            refine_reduction(*problem, search.result(), width, log)) {
      *out = refined.release();
      return PBQP_OK;
    }
  }
  *out = search.release_solution();
  return PBQP_OK;
}
} // namespace pbqp
