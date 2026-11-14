/*
 * Public C entry point for the R1/R2/RN reduction heuristic.
 *
 * Validates borrowed input, prepares an independent reduction graph, and
 * transfers solution ownership only after successful backpropagation.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"

module pbqp;
import :api_boundary;
import :solve_options;
import :error;
import :diagnostics;
import :problem;
import :reduction;

extern "C" pbqp_status pbqp_solve_reduction(const pbqp_problem *problem,
                                            const pbqp_solve_options *options,
                                            pbqp_solution **out,
                                            pbqp_error *error) {
  if (out)
    *out = nullptr;
  return pbqp::api_call(error, [&]() -> pbqp_status {
    if (!out)
      return pbqp::set_error(error, PBQP_INVALID, 0, "missing output pointer");
    const pbqp::CommonSolveOptions config(options);
    pbqp::validate(problem);
    if (auto status = config.check_heuristic(error); status != PBQP_OK)
      return status;
    const auto log = config.log;
    log.line("r1r2rn: preparing single-thread heuristic search");
    pbqp::ReductionSearch search(*problem);
    search.prepare();
    auto status = search.run(error);
    if (status != PBQP_OK)
      return status;
    *out = search.release_solution();
    return PBQP_OK;
  });
}
