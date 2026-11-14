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
#include <memory>
#include <vector>
module pbqp:bnb_worker;
import :bnb_incumbent;
import :bnb_work_queue;
import :ownership;

namespace pbqp {
// Non-template ownership keeps container destruction in this module unit.
// In particular, API consumers do not instantiate an allocator for nested
// vectors of a worker template's private Child type across module boundaries.
struct BranchAndBoundLookahead {
  struct Child {
    pbqp_cost_t bound = 0;
    bool live = false;
  };

  std::vector<std::vector<Child>> children;
  std::vector<double> weight, child_weight;
  size_t scan = 0, survivors = 0;
  bool preparing = false;

  explicit BranchAndBoundLookahead(const pbqp_problem &domains);
  ~BranchAndBoundLookahead() noexcept;
};

BranchAndBoundLookahead::BranchAndBoundLookahead(const pbqp_problem &domains)
    : children(domains.num_nodes), weight(domains.num_nodes + 1),
      child_weight(domains.num_nodes) {
  for (size_t step = 0; step < domains.num_nodes; ++step)
    children[step].resize(domains.nodes[step].num_options);
}

BranchAndBoundLookahead::~BranchAndBoundLookahead() noexcept = default;

template <typename Bounds> class BranchAndBoundWorker {
  const Bounds &bounds;
  BranchAndBoundIncumbent &incumbent;
  std::vector<size_t> tuple, cursor, limits;
  std::vector<pbqp_cost_t> parent_bound;
  typename Bounds::Workspace workspace;
  SolutionPtr best;
  // Optional child-bound storage is allocated before dispatch. Ordinary DFS
  // does not allocate or use this cache.
  std::unique_ptr<BranchAndBoundLookahead> lookahead;
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

  bool advance_filtered() noexcept {
    while (depth > base) {
      const size_t step = depth - 1;
      auto &rank = cursor[step];
      do {
        ++rank;
      } while (rank < limits[step] && !lookahead->children[step][rank].live);
      if (rank < limits[step]) {
        const size_t id = bounds.node(step);
        tuple[id] = bounds.option(id, rank);
        lookahead->weight[depth] = lookahead->child_weight[step];
        return true;
      }
      --depth;
    }
    busy = false;
    return false;
  }

  void begin_children() noexcept {
    auto &state = *lookahead;
    state.preparing = true;
    state.survivors = 0;
    state.scan = depth == base && batch ? task.child_begin : 0;
    cursor[depth] = state.scan;
    limits[depth] = depth == base && batch
                        ? task.child_end
                        : bounds.domains().nodes[depth].num_options;
  }

  void check_child() noexcept {
    auto &state = *lookahead;
    const size_t id = bounds.node(depth);
    tuple[id] = bounds.option(id, state.scan);
    auto &child = state.children[depth][state.scan++];
    if constexpr (requires { workspace.cutoff; })
      workspace.cutoff = incumbent.cost_limit();
    child.live =
        bounds.lower_bound(tuple.data(), depth + 1, workspace, child.bound) &&
        incumbent.can_improve(child.bound);
    state.survivors += child.live;
  }

  void enter_children() noexcept {
    auto &state = *lookahead;
    while (!state.children[depth][cursor[depth]].live)
      ++cursor[depth];
    const size_t id = bounds.node(depth);
    tuple[id] = bounds.option(id, cursor[depth]);
    state.child_weight[depth] = state.weight[depth] / state.survivors;
    ++depth;
    state.weight[depth] = state.child_weight[depth - 1];
  }

  template <typename Observer>
  void close_exhausted_node(Observer &observe) noexcept {
    // An initial or donated job rejected on arrival contributes no searched
    // branch. Its weight is excluded rather than immediately advancing dots.
    if (depth == base)
      observe.discarded(lookahead->weight[depth]);
    else
      observe.completed(lookahead->weight[depth]);
  }

  template <typename Observer>
  bool chunk_filtered(size_t quota, Observer &observe) noexcept {
    auto &state = *lookahead;
    for (size_t checks = 0; checks < quota; ++checks) {
      if (state.preparing) {
        check_child();
        if (state.scan < limits[depth])
          continue;
        state.preparing = false;
        if (state.survivors) {
          enter_children();
          continue;
        }
        close_exhausted_node(observe);
      } else {
        pbqp_cost_t bound;
        // Child bounds are independent of the incumbent and remain valid.
        // Recheck only the shared upper bound; a newer one can close a child
        // which survived the parent's lookahead without repeating its DP.
        bool possible;
        if (depth == base) {
          if constexpr (requires { workspace.cutoff; })
            workspace.cutoff = incumbent.cost_limit();
          possible = bounds.lower_bound(tuple.data(), depth, workspace, bound);
        } else {
          const auto &child = state.children[depth - 1][cursor[depth - 1]];
          possible = child.live;
          bound = child.bound;
        }
        if (possible && incumbent.can_improve(bound)) {
          parent_bound[depth] = bound;
          if (depth < tuple.size()) {
            begin_children();
            continue;
          }
          keep(bound);
          observe.completed(state.weight[depth]);
        } else {
          // A newer incumbent can make an already cached child trivial too.
          // Remove its weight rather than treating that cut as searched work.
          observe.discarded(state.weight[depth]);
        }
      }
      if (!advance_filtered())
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

  BranchAndBoundWorker(const Bounds &b, BranchAndBoundIncumbent &i,
                       bool estimate_progress = false)
      : bounds(b), incumbent(i), tuple(b.input().num_nodes),
        cursor(tuple.size()), limits(tuple.size()),
        parent_bound(tuple.size() + 1), workspace(b),
        best(make_solution(tuple.size())) {
    task.ranks.reserve(tuple.size());
    if (estimate_progress)
      lookahead = std::make_unique<BranchAndBoundLookahead>(bounds.domains());
  }

  bool active() const noexcept { return busy; }

  void start(pbqp_cost_t bound) noexcept {
    base = task.ranks.size();
    batch = task.child_begin != task.child_end;
    root = base + (batch && !lookahead);
    depth = root;
    busy = true;
    if (lookahead) {
      lookahead->preparing = false;
      lookahead->weight[base] = task.progress_weight;
    }
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
    if constexpr (requires {
                    bounds.prepare_prefix(tuple.data(), base, workspace,
                                          incumbent.cost_limit());
                  })
      bounds.prepare_prefix(tuple.data(), base, workspace,
                            incumbent.cost_limit());
  }

  template <typename Observer>
  bool chunk(size_t quota, Observer &&observe) noexcept {
    if constexpr (requires { observe.completed(0.0); }) {
      return chunk_filtered(quota, observe);
    } else {
      for (size_t checks = 0; checks < quota; ++checks) {
        pbqp_cost_t bound;
        if constexpr (requires { workspace.cutoff; })
          workspace.cutoff = incumbent.cost_limit();
        bool possible =
            bounds.lower_bound(tuple.data(), depth, workspace, bound);
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
  }

  bool share(CoarseWorkQueue &queue) {
    if (!queue.wants_work())
      return false;
    for (size_t step = base; step < depth; ++step) {
      if (cursor[step] + 1 >= limits[step])
        continue;
      double weight = 0;
      if (lookahead) {
        size_t live = 0;
        for (size_t rank = cursor[step] + 1; rank < limits[step]; ++rank)
          live += lookahead->children[step][rank].live;
        if (!live)
          continue;
        weight = live * lookahead->child_weight[step];
      }
      if (queue.donate(cursor.data(), step, cursor[step] + 1, limits[step],
                       parent_bound[step], weight)) {
        limits[step] = cursor[step] + 1;
        return true;
      }
    }
    return false;
  }
};
} // namespace pbqp
