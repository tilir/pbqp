/*
 * Internal bruteforce API orchestration: option checks, validated borrowed
 * input, owned search preparation, and result transfer. Exceptions are
 * translated only by the thin public C entry point.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
module pbqp:bruteforce_api;
import :deadline;
import :solve_options;
import :bruteforce;
import :bruteforce_parallel;
import :error;
import :diagnostics;
import :problem;

namespace pbqp {
pbqp_status solve_bruteforce(const pbqp_problem *p,
                             const pbqp_solve_options *options,
                             pbqp_solution **out, pbqp_error *error) {
  if (!out)
    return set_error(error, PBQP_INVALID, 0, "missing output pointer");
  const CommonSolveOptions config(options);
  const auto deadline = config.deadline();
  size_t threads = config.threads;
  validate(p);
  if (auto status = config.check_unknown(error); status != PBQP_OK)
    return status;
  const auto log = config.log;
  log.line("naive: exhaustive lexicographic search; requested threads=",
           threads);
  if (deadline.expired())
    return set_error(error, PBQP_TIMEOUT, 0, "exact search timed out");
  if (deadline.enabled())
    log.line("options: timeout-ms=", config.view.size("timeout-ms", 0));
  auto progress = config.progress;
  auto data = config.progress_data;
  if (threads == 1) {
    BruteForceSearch search(*p, progress, data, deadline);
    search.prepare();
    auto status = search.run(error);
    if (status != PBQP_OK)
      return status;
    *out = search.release_solution();
  } else {
    ParallelBruteForceSearch search(*p, threads, progress, data, deadline);
    search.prepare();
    auto status = search.run(error);
    if (status != PBQP_OK)
      return status;
    *out = search.release_solution();
  }
  return PBQP_OK;
}
} // namespace pbqp
