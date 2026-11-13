/*
 * Bound-aware prefix planning for branch-and-bound jobs.
 *
 * Filters the shared frontier through conditioned bounds and canonical register
 * names. Complete feasible leaves improve the incumbent before pool execution;
 * the shared planner records every closed prefix for deferred progress.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <vector>

module pbqp:bnb_tasks;
import :bnb_incumbent;
import :search_tasks;

namespace pbqp {
template <class Bounds> class PrefixPlanner {
  const Bounds &bounds;
  BranchAndBoundIncumbent &incumbent;
  typename Bounds::Workspace workspace;
  std::vector<size_t> tuple;

  bool retain(const PrefixTask &task);

public:
  PrefixPlanner(const Bounds &b, BranchAndBoundIncumbent &i)
      : bounds(b), incumbent(i), workspace(b), tuple(b.input().num_nodes) {}
  PrefixPlan build(size_t threads) {
    return plan_prefixes(bounds.domains(), threads,
                         [&](const PrefixTask &task) { return retain(task); });
  }
};

template <class Bounds>
bool PrefixPlanner<Bounds>::retain(const PrefixTask &task) {
  for (size_t step = 0; step < task.ranks.size(); ++step) {
    size_t node = bounds.node(step);
    tuple[node] = bounds.option(node, task.ranks[step]);
  }
  pbqp_cost_t cost;
  if (!bounds.lower_bound(tuple.data(), task.ranks.size(), workspace, cost) ||
      !incumbent.can_improve(cost))
    return false;
  if (task.ranks.size() == tuple.size()) {
    incumbent.update(tuple.data(), cost);
    return false;
  }
  return true;
}
} // namespace pbqp
