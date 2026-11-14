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
import :diagnostics;
import :problem;
import :branch_and_bound;

extern "C" {
pbqp_status pbqp_solve_branch_and_bound(const pbqp_problem *p,
                                        const pbqp_solve_options *options,
                                        pbqp_solution **out,
                                        pbqp_error *error) {
  if (out)
    *out = nullptr;
  return pbqp::api_call(error, [&]() -> pbqp_status {
    if (!out)
      return pbqp::set_error(error, PBQP_INVALID, 0, "missing output pointer");
    size_t threads = pbqp::thread_count(options);
    pbqp::validate(p);
    const pbqp::Diagnostics log(options ? options->diagnostics : nullptr,
                                options ? options->diagnostic_data : nullptr);
    pbqp::BranchAndBoundSearch search(
        *p, threads, options ? options->progress : nullptr,
        options ? options->user_data : nullptr, log);
    search.prepare();
    auto status = search.run(error);
    if (status != PBQP_OK)
      return status;
    *out = search.release_solution();
    return PBQP_OK;
  });
}
} // extern "C"
