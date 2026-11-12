/*
 * Shared feasible incumbent for sequential and parallel branch-and-bound.
 *
 * Frequent bound checks read atomics only. Rare improvements lock, recheck,
 * copy the assignment, and publish its reference-order cost. A stale cost
 * allows extra work but can never invalidate a pruning decision.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <atomic>
#include <mutex>

module pbqp:bnb_incumbent;
import :ownership;

namespace pbqp {
class BranchAndBoundIncumbent {
  SolutionPtr solution;
  std::atomic<pbqp_cost_t> upper_bound{PBQP_INF};
  std::atomic<bool> feasible{false};
  std::mutex mutex;

public:
  explicit BranchAndBoundIncumbent(size_t count)
      : solution(make_solution(count)) {}
  bool can_improve(pbqp_cost_t bound) const noexcept {
    // Acquire pairs with the first feasibility publication and subsequent cost
    // publications below. Older observations only retain a weaker upper bound;
    // these reads never access the assignment, which is updated under the
    // mutex.
    return !feasible.load(std::memory_order_acquire) ||
           bound < upper_bound.load(std::memory_order_acquire);
  }
  bool found() const noexcept {
    // Acquire pairs with publishing the first complete feasible incumbent.
    return feasible.load(std::memory_order_acquire);
  }
  void update(const size_t *options, pbqp_cost_t cost) noexcept {
    if (!can_improve(cost))
      return;
    std::scoped_lock lock(mutex);
    if (!can_improve(cost))
      return;
    if (solution->num_nodes)
      std::copy_n(options, solution->num_nodes, solution->options);
    solution->cost = cost;
    // Release makes the cost publication the commit point after copying the
    // complete winner. The acquire cost reads above pair with this publication.
    upper_bound.store(cost, std::memory_order_release);
    // Release publishes the initial cost even when it is +INF; acquire reads
    // of feasible must distinguish that valid incumbent from no solution.
    feasible.store(true, std::memory_order_release);
  }
  pbqp_solution &result() noexcept { return *solution; }
  pbqp_solution *release_solution() noexcept { return solution.release(); }
};
} // namespace pbqp
