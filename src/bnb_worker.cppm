/*
 * Per-worker iterative DFS over a fixed branch-and-bound prefix.
 *
 * All assignments, option cursors, conditioned-bound scratch, and local best
 * storage belong to this worker. Shared input and minima are read-only.
 * No allocation or exception handling occurs inside subtree search.
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
import :bnb_bounds;
import :bnb_incumbent;
import :search_tasks;
import :ownership;

namespace pbqp {
class BranchAndBoundWorker {
  const BranchAndBoundBounds &bounds;
  BranchAndBoundIncumbent &incumbent;
  std::vector<size_t> tuple;
  std::vector<size_t> cursor;
  BranchAndBoundBounds::Workspace workspace;
  SolutionPtr best;
  bool found = false;

  bool advance(size_t &depth, size_t root) noexcept {
    while (depth > root) {
      size_t node = depth - 1;
      if (++cursor[node] < bounds.input().nodes[node].num_options) {
        tuple[node] = bounds.option(node, cursor[node]);
        return true;
      }
      --depth;
    }
    return false;
  }
  void keep_assignment(pbqp_cost_t cost) noexcept {
    if (!found || cost < best->cost) {
      found = true;
      best->cost = cost;
      if (!tuple.empty())
        std::copy(tuple.begin(), tuple.end(), best->options);
      incumbent.update(best->options, cost);
    }
  }

public:
  BranchAndBoundWorker(const BranchAndBoundBounds &b,
                       BranchAndBoundIncumbent &i)
      : bounds(b), incumbent(i), tuple(b.input().num_nodes),
        cursor(b.input().num_nodes), workspace(b),
        best(make_solution(b.input().num_nodes)) {}

  template <class Observer>
  void search_prefix(size_t root, Observer observe) noexcept {
    size_t depth = root;
    for (;;) {
      pbqp_cost_t bound;
      if (bounds.lower_bound(tuple.data(), depth, workspace, bound) &&
          incumbent.can_improve(bound)) {
        if (depth < tuple.size()) {
          cursor[depth] = 0;
          tuple[depth] = bounds.option(depth, 0);
          ++depth;
          continue;
        }
        keep_assignment(bound);
      }
      observe(depth);
      if (!advance(depth, root))
        break;
    }
  }
  template <class Observer>
  void search(const PrefixTask &task, Observer observe) noexcept {
    size_t root = task.ranks.size();
    for (size_t id = 0; id < root; ++id) {
      cursor[id] = task.ranks[id];
      tuple[id] = bounds.option(id, cursor[id]);
    }
    if (task.child_begin == task.child_end) {
      search_prefix(root, observe);
      return;
    }
    for (size_t rank = task.child_begin; rank < task.child_end; ++rank) {
      cursor[root] = rank;
      tuple[root] = bounds.option(root, rank);
      search_prefix(root + 1, observe);
    }
  }
};
} // namespace pbqp
