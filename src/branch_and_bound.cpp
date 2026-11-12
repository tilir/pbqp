/*
 * Public C entry points for exact branch-and-bound search.
 *
 * Validates borrowed inputs and transfers independent solution storage only
 * after search has proved optimality. Optional callbacks report covered space.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"

module pbqp;
import :api_boundary;
import :error;
import :problem;
import :branch_and_bound;

extern "C" {
pbqp_status pbqp_solve_branch_and_bound(const pbqp_problem *p,
                                        pbqp_solution **out,
                                        pbqp_error *error) {
  return pbqp_solve_branch_and_bound_with_progress(p, nullptr, nullptr, out,
                                                   error);
}

pbqp_status pbqp_solve_branch_and_bound_with_progress(
    const pbqp_problem *p, pbqp_progress_callback progress, void *user_data,
    pbqp_solution **out, pbqp_error *error) {
  if (out)
    *out = nullptr;
  return pbqp::api_call(error, [&]() -> pbqp_status {
    if (!out)
      return pbqp::set_error(error, PBQP_INVALID, 0, "missing output pointer");
    pbqp::validate(p);
    pbqp::BranchAndBoundSearch search(*p, progress, user_data);
    auto status = search.prepare(error);
    if (status != PBQP_OK)
      return status;
    status = search.run(error);
    if (status != PBQP_OK)
      return status;
    *out = search.release_solution();
    return PBQP_OK;
  });
}
} // extern "C"
