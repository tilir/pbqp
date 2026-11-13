/*
 * Shared coarse prefix planning for parallel exact solvers.
 *
 * Builds a bounded frontier with heterogeneous domains and batches unusually
 * large child sets. A filter may close prefixes during B&B planning; exhaustive
 * search retains every prefix, including ones with forbidden assignments.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <atomic>
#include <deque>
#include <limits>
#include <utility>
#include <vector>

module pbqp:search_tasks;

namespace pbqp {
class PrefixQueue {
  std::atomic<size_t> next{0};

public:
  bool claim(size_t count, size_t &task) noexcept {
    // This counter grants unique indices only. TBB launch publishes the plan
    // and workspaces; no data is published by either atomic operation, so
    // relaxed ordering is sufficient for both the load and the ticket CAS.
    task = next.load(std::memory_order_relaxed);
    while (task < count)
      if (next.compare_exchange_weak(task, task + 1, std::memory_order_relaxed))
        return true;
    return false;
  }
};

struct PrefixTask {
  std::vector<size_t> ranks;
  size_t child_begin = 0;
  size_t child_end = 0;
};
struct PrefixPlan {
  std::vector<PrefixTask> tasks;
  std::vector<size_t> covered_depths;
};

void batch_children(PrefixPlan &plan, const PrefixTask &parent, size_t count,
                    size_t slots) {
  size_t width = count / slots + (count % slots != 0);
  for (size_t begin = 0; begin < count;) {
    size_t end = begin + std::min(width, count - begin);
    plan.tasks.push_back({parent.ranks, begin, end});
    begin = end;
  }
}

template <class Keep>
PrefixPlan plan_prefixes(const pbqp_problem &problem, size_t threads,
                         Keep keep) {
  PrefixPlan plan;
  const auto limit = std::numeric_limits<size_t>::max();
  size_t target = threads > limit / 32 ? limit : threads * 32;
  size_t budget = target > limit / 8 ? limit : target * 8;
  auto retain = [&](const PrefixTask &task) {
    if (keep(task))
      return true;
    plan.covered_depths.push_back(task.ranks.size());
    return false;
  };
  std::deque<PrefixTask> frontier;
  PrefixTask root;
  if (retain(root))
    frontier.push_back(std::move(root));
  for (size_t expanded = 0;
       !frontier.empty() && frontier.size() + plan.tasks.size() < target &&
       expanded < budget;
       ++expanded) {
    auto parent = std::move(frontier.front());
    frontier.pop_front();
    if (parent.ranks.size() == problem.num_nodes) {
      plan.tasks.push_back(std::move(parent));
      continue;
    }
    size_t domain = problem.nodes[parent.ranks.size()].num_options;
    size_t slots = target - frontier.size() - plan.tasks.size();
    if (domain > slots) {
      batch_children(plan, parent, domain, slots);
      continue;
    }
    for (size_t rank = 0; rank < domain; ++rank) {
      PrefixTask child = parent;
      child.ranks.push_back(rank);
      if (retain(child))
        frontier.push_back(std::move(child));
    }
  }
  while (!frontier.empty()) {
    plan.tasks.push_back(std::move(frontier.front()));
    frontier.pop_front();
  }
  return plan;
}
} // namespace pbqp
