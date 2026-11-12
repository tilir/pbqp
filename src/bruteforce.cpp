/*
 * Exhaustive PBQP reference solver.
 *
 * Enumerates option tuples in lexicographic order and retains the first
 * optimum. The enumeration uses status returns and performs no exception
 * handling.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"

module pbqp;
import :api_boundary;
import :bruteforce;
import :error;
import :problem;

extern "C" {
pbqp_status pbqp_solve_bruteforce(const pbqp_problem *p, pbqp_solution **out,
                                  pbqp_error *error) {
  return pbqp_solve_bruteforce_with_progress(p, nullptr, nullptr, out, error);
}

pbqp_status pbqp_solve_bruteforce_with_progress(const pbqp_problem *p,
                                                pbqp_progress_callback progress,
                                                void *user_data,
                                                pbqp_solution **out,
                                                pbqp_error *error) {
  if (out)
    *out = nullptr;
  return api_call(error, [&]() -> pbqp_status {
    if (!out)
      return pbqp_fail(error, PBQP_INVALID, 0, "missing output pointer");
    validate(p);
    BruteForceSearch search(*p, progress, user_data);
    search.prepare();
    auto status = search.run(error);
    if (status != PBQP_OK)
      return status;
    *out = search.release_solution();
    return PBQP_OK;
  });
}
} // extern "C"
