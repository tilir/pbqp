/*
 * Internal RAII ownership for C API objects.
 *
 * Provides smart-pointer aliases using the public problem and solution
 * deleters. Caller-owned problem views are never owned by these aliases.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;

#include "pbqp.h"
#include <memory>

module pbqp:ownership;

inline constexpr auto problem_deleter = [](pbqp_problem *problem) noexcept {
  pbqp_problem_free(problem);
};
inline constexpr auto solution_deleter = [](pbqp_solution *solution) noexcept {
  pbqp_solution_free(solution);
};

using ProblemPtr = std::unique_ptr<pbqp_problem, decltype(problem_deleter)>;
using SolutionPtr = std::unique_ptr<pbqp_solution, decltype(solution_deleter)>;
