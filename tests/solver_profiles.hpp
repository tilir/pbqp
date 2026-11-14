/*
 * Explicit profile selection for existing differential solver test callables.
 * Own the temporary option array for the duration of the synchronous C call.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
#pragma once
#include "pbqp.h"
#include <cstddef>
#include <vector>

namespace pbqp_test {
template <std::size_t Level>
inline pbqp_status solve_profile(const pbqp_problem *problem,
                                 const pbqp_solve_options *options,
                                 pbqp_solution **out, pbqp_error *error) {
  std::vector<pbqp_option> entries;
  if (options && options->count)
    entries.assign(options->entries, options->entries + options->count);
  entries.push_back(pbqp_size_option("optimization-level", Level));
  const pbqp_solve_options config{entries.size(), entries.data()};
  return pbqp_solve_branch_and_bound(problem, &config, out, error);
}
} // namespace pbqp_test
