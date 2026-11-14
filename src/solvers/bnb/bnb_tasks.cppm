/*
 * Work-budgeted B&B frontier with conditioned-bound priorities.
 * Budget exhaustion retains unchecked sibling ranges as complete coarse jobs;
 * no unvisited tuple disappears from the plan or from progress accounting.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <chrono>
#include <utility>
#include <vector>
module pbqp:bnb_tasks;
import :bnb_incumbent;
import :bnb_work_queue;
namespace pbqp {
template <typename Bounds> class PrefixPlanner {
  const Bounds &bounds;
  BranchAndBoundIncumbent &incumbent;
  typename Bounds::Workspace workspace;
  std::vector<size_t> tuple;
  AdaptivePlan plan;
  std::vector<RankedPrefix> frontier;
  size_t sequence = 0;
  std::chrono::steady_clock::time_point started;

  bool exhausted() {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - started)
                       .count();
    if (plan.calls >= BranchAndBoundSettings::planning_calls) {
      plan.call_budget_hit = true;
      return true;
    }
    if (static_cast<size_t>(elapsed) >= BranchAndBoundSettings::planning_ms) {
      plan.time_budget_hit = true;
      return true;
    }
    return false;
  }
  bool retain(RankedPrefix &prefix) {
    for (size_t step = 0; step < prefix.task.ranks.size(); ++step) {
      size_t node = bounds.node(step);
      tuple[node] = bounds.option(node, prefix.task.ranks[step]);
    }
    ++plan.calls;
    prefix.sequence = sequence++;
    if (!bounds.lower_bound(tuple.data(), prefix.task.ranks.size(), workspace,
                            prefix.bound) ||
        !incumbent.can_improve(prefix.bound)) {
      plan.covered_depths.push_back(prefix.task.ranks.size());
      return false;
    }
    if (prefix.task.ranks.size() == tuple.size()) {
      incumbent.update(tuple.data(), prefix.bound);
      plan.covered_depths.push_back(prefix.task.ranks.size());
      return false;
    }
    return true;
  }
  RankedPrefix take_preferred() {
    auto best = frontier.begin();
    for (auto it = best + 1; it != frontier.end(); ++it)
      if (prefix_preferred(it->bound, it->sequence, best->bound,
                           best->sequence))
        best = it;
    RankedPrefix result = std::move(*best);
    frontier.erase(best);
    return result;
  }
  void expand(RankedPrefix parent, size_t target) {
    size_t original = parent.task.ranks.size();
    while (parent.task.ranks.size() < tuple.size() &&
           bounds.domains().nodes[parent.task.ranks.size()].num_options == 1)
      parent.task.ranks.push_back(0);
    // Forced runs have one possible prefix. Check the compressed assignment
    // once, preserving the work budget for branching beyond the run.
    if (parent.task.ranks.size() != original && !retain(parent))
      return;
    size_t depth = parent.task.ranks.size();
    size_t domain = bounds.domains().nodes[depth].num_options, rank = 0;
    for (; rank < domain; ++rank) {
      if (exhausted() || frontier.size() + plan.tasks.size() + 1 >= target)
        break;
      RankedPrefix child = parent;
      child.task.ranks.push_back(rank);
      if (retain(child))
        frontier.push_back(std::move(child));
    }
    if (rank < domain) {
      parent.task.child_begin = rank;
      parent.task.child_end = domain;
      parent.sequence = sequence++;
      plan.tasks.push_back(std::move(parent));
    }
  }

public:
  PrefixPlanner(const Bounds &b, BranchAndBoundIncumbent &i)
      : bounds(b), incumbent(i), workspace(b), tuple(b.input().num_nodes) {}
  AdaptivePlan build(const BranchAndBoundSettings &settings) {
    started = std::chrono::steady_clock::now();
    RankedPrefix root;
    if (retain(root))
      frontier.push_back(std::move(root));
    while (!frontier.empty() &&
           frontier.size() + plan.tasks.size() < settings.jobs && !exhausted())
      expand(take_preferred(), settings.jobs);
    for (auto &prefix : frontier)
      plan.tasks.push_back(std::move(prefix));
    return std::move(plan);
  }
};
} // namespace pbqp
