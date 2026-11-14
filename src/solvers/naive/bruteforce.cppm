/*
 * Exhaustive search state and lexicographic tuple enumeration.
 *
 * Prepares vector-backed workspace before searching. The tuple loop performs
 * no allocation or exception handling and retains the first minimum in
 * reference evaluation order. Progress observation is optional.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <vector>

module pbqp:bruteforce;
import :error;
import :evaluation;
import :ownership;
import :progress;

namespace pbqp {
class BruteForceSearch {
  const pbqp_problem &problem;
  pbqp_progress_callback progress;
  void *user_data;
  SolutionPtr solution;
  std::vector<size_t> tuple;
  std::vector<size_t> boundary;

  bool advance() noexcept;
  template <typename Observer> bool search(Observer observe) noexcept;

public:
  BruteForceSearch(const pbqp_problem &p, pbqp_progress_callback callback,
                   void *data) noexcept
      : problem(p), progress(callback), user_data(data) {}

  void prepare();
  pbqp_status run(pbqp_error *error) noexcept;

  pbqp_solution *release_solution() noexcept { return solution.release(); }
};

void BruteForceSearch::prepare() {
  solution = make_solution(problem.num_nodes);
  tuple.assign(problem.num_nodes, 0);
  if (progress)
    boundary.resize(problem.num_nodes);
}

bool BruteForceSearch::advance() noexcept {
  for (size_t k = problem.num_nodes; k > 0; --k) {
    if (++tuple[k - 1] < problem.nodes[k - 1].num_options)
      return true;
    tuple[k - 1] = 0;
  }
  return false;
}

template <typename Observer>
bool BruteForceSearch::search(Observer observe) noexcept {
  bool found = false;
  do {
    pbqp_cost_t cost;
    if (evaluate_assignment(problem, tuple.data(), cost) &&
        (!found || cost < solution->cost)) {
      found = true;
      solution->cost = cost;
      if (problem.num_nodes)
        std::copy_n(tuple.data(), problem.num_nodes, solution->options);
    }
    observe(tuple.data());
  } while (advance());
  return found;
}

pbqp_status BruteForceSearch::run(pbqp_error *error) noexcept {
  bool found;
  if (progress) {
    PercentProgress reporter(problem, progress, user_data, boundary.data());
    found = search([&](const size_t *completed) noexcept {
      reporter.completed(completed);
    });
  } else {
    // The no-op observer is compiled away when progress is disabled.
    found = search([](const size_t *) noexcept {});
  }
  if (!found)
    return set_error(error, PBQP_INFEASIBLE, 0, "no feasible tuple");
  solution->quality = PBQP_SOLUTION_OPTIMAL;
  return PBQP_OK;
}
} // namespace pbqp
