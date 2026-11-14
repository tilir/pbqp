/*
 * Shared C ownership boundary and option resolution for exact B&B search.
 * All B&B profiles use one bounds strategy and the same search engine.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <array>
#include <string_view>
module pbqp:bnb_api;
import :api_boundary;
import :solve_options;
import :failure;
import :error;
import :problem;
import :bnb_options;
import :branch_and_bound;
import :bnb_bounds;

namespace pbqp {
BnbOptions read_bnb_options(const SolveOptionsView &view) {
  const size_t level = view.size("optimization-level", 1);
  if (level > 2)
    fail(PBQP_INVALID, "optimization-level must be 0, 1, or 2");
  const auto profile = static_cast<BnbProfile>(level);
  const BnbOptions defaults(profile);
  unsigned enable = 0, disable = 0;
  for (const auto &entry : bnb_optimizations) {
    const auto bit = static_cast<unsigned>(entry.optimization);
    if (view.boolean(entry.key, defaults.enabled(entry.optimization)))
      enable |= bit;
    else
      disable |= bit;
  }
  return BnbOptions(profile, enable, disable);
}

pbqp_status solve_bnb(const pbqp_problem *p, const pbqp_solve_options *options,
                      pbqp_solution **out, pbqp_error *error) noexcept {
  if (out)
    *out = nullptr;
  return api_call(error, [&]() -> pbqp_status {
    if (!out)
      return set_error(error, PBQP_INVALID, 0, "missing output pointer");
    const CommonSolveOptions config(options);
    const auto optimizations = read_bnb_options(config.view);
    validate(p);
    if (auto status = config.check_unknown(error); status != PBQP_OK)
      return status;
    config.log.line("options: optimization profile=O",
                    config.view.size("optimization-level", 1),
                    "; explicit overrides apply after the profile");
    config.log.line("options: optimization settings (input eligibility and "
                    "budgets checked during preparation)");
    if (config.log)
      for (const auto &entry : bnb_optimizations)
        config.log.line("options: ", entry.key, "=",
                        optimizations.enabled(entry.optimization) ? "enabled"
                                                                  : "disabled");
    if (optimizations.requested(BnbOptimization::Repricing) &&
        !optimizations.enabled(BnbOptimization::WeightedBounds))
      config.log.line("options: repricing requires weighted-bounds");
    BranchAndBoundSearch<BranchAndBoundBounds> search(
        *p, config.threads, config.progress, config.progress_data, config.log,
        optimizations);
    search.prepare();
    auto status = search.run(error);
    if (status != PBQP_OK)
      return status;
    *out = search.release_solution();
    return PBQP_OK;
  });
}
} // namespace pbqp
