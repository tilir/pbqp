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
import :bruteforce_parallel;
import :error;
import :diagnostics;
import :problem;

extern "C" {
pbqp_status pbqp_solve_bruteforce(const pbqp_problem *p,
                                  const pbqp_solve_options *options,
                                  pbqp_solution **out, pbqp_error *error) {
  if (out)
    *out = nullptr;
  return pbqp::api_call(error, [&]() -> pbqp_status {
    if (!out)
      return pbqp::set_error(error, PBQP_INVALID, 0, "missing output pointer");
    size_t threads = pbqp::thread_count(options);
    pbqp::validate(p);
    const pbqp::Diagnostics log(options ? options->diagnostics : nullptr,
                                options ? options->diagnostic_data : nullptr);
    log.line("naive: exhaustive lexicographic search; requested threads=",
             threads);
    auto progress = options ? options->progress : nullptr;
    auto data = options ? options->user_data : nullptr;
    if (threads == 1) {
      pbqp::BruteForceSearch search(*p, progress, data);
      search.prepare();
      auto status = search.run(error);
      if (status != PBQP_OK)
        return status;
      *out = search.release_solution();
    } else {
      pbqp::ParallelBruteForceSearch search(*p, threads, progress, data);
      search.prepare();
      auto status = search.run(error);
      if (status != PBQP_OK)
        return status;
      *out = search.release_solution();
    }
    return PBQP_OK;
  });
}
} // extern "C"
