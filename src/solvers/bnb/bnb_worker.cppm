/*
 * Resumable allocation-free DFS with coarse sibling donation between chunks.
 * A donated range is removed from the donor's cursor limits. The current path
 * stays private; progress records only closed, disjoint prefixes.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <vector>
module pbqp:bnb_worker;
import :bnb_incumbent;
import :bnb_work_queue;
import :ownership;
namespace pbqp {
template <typename Bounds> class BranchAndBoundWorker {
  const Bounds &bounds;
  BranchAndBoundIncumbent &incumbent;
  std::vector<size_t> tuple, cursor, limits;
  std::vector<pbqp_cost_t> parent_bound;
  typename Bounds::Workspace workspace;
  SolutionPtr best;
  bool found = false, batch = false, busy = false;
  size_t base = 0, root = 0, depth = 0;
  bool advance() noexcept {
    while (depth > root) {
      size_t step = depth - 1, id = bounds.node(step);
      if (++cursor[step] < limits[step]) {
        tuple[id] = bounds.option(id, cursor[step]);
        return true;
      }
      --depth;
    }
    if (batch && ++cursor[base] < limits[base]) {
      tuple[bounds.node(base)] = bounds.option(bounds.node(base), cursor[base]);
      depth = root;
      return true;
    }
    return false;
  }
  void keep(pbqp_cost_t cost) noexcept {
    if (!found || cost < best->cost) {
      found = true;
      best->cost = cost;
      std::copy(tuple.begin(), tuple.end(), best->options);
      incumbent.update(best->options, cost);
    }
  }

public:
  PrefixTask task;
  BranchAndBoundWorker(const Bounds &b, BranchAndBoundIncumbent &i)
      : bounds(b), incumbent(i), tuple(b.input().num_nodes),
        cursor(tuple.size()), limits(tuple.size()),
        parent_bound(tuple.size() + 1), workspace(b),
        best(make_solution(tuple.size())) {
    task.ranks.reserve(tuple.size());
  }
  bool active() const noexcept { return busy; }
  void start(pbqp_cost_t bound) noexcept {
    base = task.ranks.size();
    batch = task.child_begin != task.child_end;
    root = base + batch;
    depth = root;
    busy = true;
    for (size_t step = 0; step < base; ++step) {
      cursor[step] = task.ranks[step];
      size_t id = bounds.node(step);
      tuple[id] = bounds.option(id, cursor[step]);
    }
    parent_bound[base] = bound;
    if (batch) {
      cursor[base] = task.child_begin;
      limits[base] = task.child_end;
      size_t id = bounds.node(base);
      tuple[id] = bounds.option(id, cursor[base]);
    }
  }
  template <typename Observer>
  bool chunk(size_t quota, Observer observe) noexcept {
    for (size_t checks = 0; checks < quota; ++checks) {
      pbqp_cost_t bound;
      bool possible = bounds.lower_bound(tuple.data(), depth, workspace, bound);
      if (possible && incumbent.can_improve(bound)) {
        parent_bound[depth] = bound;
        if (depth < tuple.size()) {
          cursor[depth] = 0;
          limits[depth] = bounds.domains().nodes[depth].num_options;
          size_t id = bounds.node(depth);
          tuple[id] = bounds.option(id, 0);
          ++depth;
          continue;
        }
        keep(bound);
      }
      observe(depth);
      if (!advance()) {
        busy = false;
        return true;
      }
    }
    return false;
  }
  bool share(CoarseWorkQueue &queue) {
    if (!queue.wants_work())
      return false;
    for (size_t step = base; step < depth; ++step)
      if (cursor[step] + 1 < limits[step] &&
          queue.donate(cursor.data(), step, cursor[step] + 1, limits[step],
                       parent_bound[step])) {
        limits[step] = cursor[step] + 1;
        return true;
      }
    return false;
  }
};
} // namespace pbqp
