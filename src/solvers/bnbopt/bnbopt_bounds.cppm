/*
 * Optimized exact bounds with clique capacity and reference-order spill DP.
 *
 * Verified small regalloc inputs use bit-mask forward checking, structural
 * branching order, and clique spill-count relaxations. Each relaxation performs
 * the original unary additions in node-ID order. General inputs retain the
 * ordinary B&B bounds; no real-sum estimate is used to prune rounded costs.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <array>
#include <bit>

module pbqp:bnbopt_bounds;
import :bnb_bounds;
import :bnbopt_structure;

namespace pbqp {
class OptimizedBranchAndBoundBounds {
  using Mask = RegallocStructure::Mask;
  static constexpr size_t capacity = RegallocStructure::capacity;
  BranchAndBoundBounds reference;
  RegallocStructure structure;

public:
  struct Workspace {
    BranchAndBoundBounds::Workspace baseline;
    std::array<Mask, capacity> colored{}, allowed{};
    std::array<pbqp_cost_t, capacity> terms{};
    std::array<pbqp_cost_t, capacity + 1> current{}, next{};
    std::array<bool, capacity + 1> current_live{}, next_live{};
    Mask assigned = 0;
    explicit Workspace(const OptimizedBranchAndBoundBounds &bounds)
        : baseline(bounds.reference) {}
  };

private:
  bool prepare_terms(const size_t *tuple, size_t depth, Workspace &workspace,
                     pbqp_cost_t &cost) const noexcept;
  bool clique_bound(Mask pending, size_t required, Workspace &workspace,
                    pbqp_cost_t &cost) const noexcept;
  bool strengthen(Mask clique, Workspace &workspace,
                  pbqp_cost_t &cost) const noexcept;
  static void update(Workspace &workspace, size_t state,
                     pbqp_cost_t value) noexcept {
    if (!workspace.next_live[state] || value < workspace.next[state]) {
      workspace.next_live[state] = true;
      workspace.next[state] = value;
    }
  }

public:
  explicit OptimizedBranchAndBoundBounds(const pbqp_problem &p)
      : reference(p), structure(p, reference.register_symmetric()) {}
  const pbqp_problem &input() const noexcept { return reference.input(); }
  const pbqp_problem &domains() const noexcept { return structure.domains(); }
  size_t node(size_t step) const noexcept { return structure.node(step); }
  size_t option(size_t node, size_t rank) const noexcept {
    return reference.option(node, rank);
  }
  void prepare_search() { structure.prepare(); }
  bool optimized() const noexcept { return structure.ready(); }
  const RegallocStructure &graph() const noexcept { return structure; }
  bool lower_bound(const size_t *tuple, size_t depth, Workspace &workspace,
                   pbqp_cost_t &cost) const noexcept;
};

bool OptimizedBranchAndBoundBounds::prepare_terms(
    const size_t *tuple, size_t depth, Workspace &workspace,
    pbqp_cost_t &cost) const noexcept {
  workspace.colored.fill(0);
  workspace.assigned = 0;
  size_t used = 0;
  for (size_t step = 0; step < depth; ++step) {
    size_t id = node(step), option = tuple[id];
    if (option > used + 1 || input().nodes[id].costs[option] == PBQP_INF)
      return false;
    if (option) {
      if (structure.neighbors(id) & workspace.colored[option - 1])
        return false;
      workspace.colored[option - 1] |= RegallocStructure::bit(id);
      used = std::max(used, option);
    }
    workspace.assigned |= RegallocStructure::bit(id);
  }
  cost = 0;
  Mask registers = RegallocStructure::full(input().nodes[0].num_options - 1);
  for (size_t id = 0; id < input().num_nodes; ++id) {
    pbqp_cost_t term;
    if (workspace.assigned & RegallocStructure::bit(id)) {
      term = input().nodes[id].costs[tuple[id]];
      workspace.allowed[id] = 0;
    } else {
      Mask allowed = registers;
      for (size_t color = 0; color < used; ++color)
        if (structure.neighbors(id) & workspace.colored[color])
          allowed &= ~RegallocStructure::bit(color);
      workspace.allowed[id] = allowed;
      auto spill = input().nodes[id].costs[0];
      term = allowed ? std::min(spill, pbqp_cost_t{0}) : spill;
      if (term == PBQP_INF)
        return false;
    }
    workspace.terms[id] = term;
    cost = pbqp_cost_add(cost, term);
  }
  return true;
}

bool OptimizedBranchAndBoundBounds::clique_bound(
    Mask pending, size_t required, Workspace &workspace,
    pbqp_cost_t &cost) const noexcept {
  // +INF is a valid reachable rounded score. Liveness cannot use a sentinel.
  workspace.current_live.fill(false);
  workspace.current_live[0] = true;
  workspace.current[0] = 0;
  // State 'required' also represents larger spill counts. Future choices in
  // this relaxation depend only on the state, so the cheapest prefix suffices.
  for (size_t id = 0; id < input().num_nodes; ++id) {
    if (!(pending & RegallocStructure::bit(id))) {
      for (size_t state = 0; state <= required; ++state)
        if (workspace.current_live[state])
          workspace.current[state] =
              pbqp_cost_add(workspace.current[state], workspace.terms[id]);
      continue;
    }
    workspace.next_live.fill(false);
    auto spill = input().nodes[id].costs[0];
    for (size_t state = 0; state <= required; ++state) {
      if (!workspace.current_live[state])
        continue;
      if (workspace.allowed[id])
        update(workspace, state, pbqp_cost_add(workspace.current[state], 0));
      if (spill != PBQP_INF)
        update(workspace, std::min(required, state + 1),
               pbqp_cost_add(workspace.current[state], spill));
    }
    std::copy_n(workspace.next.begin(), required + 1,
                workspace.current.begin());
    std::copy_n(workspace.next_live.begin(), required + 1,
                workspace.current_live.begin());
  }
  if (!workspace.current_live[required])
    return false;
  cost = std::max(cost, workspace.current[required]);
  return true;
}

bool OptimizedBranchAndBoundBounds::strengthen(
    Mask clique, Workspace &workspace, pbqp_cost_t &cost) const noexcept {
  Mask pending = clique & ~workspace.assigned;
  Mask colors = 0;
  size_t forced = 0, can_spill = 0;
  for (Mask remaining = pending; remaining; remaining &= remaining - 1) {
    size_t id = std::countr_zero(remaining);
    colors |= workspace.allowed[id];
    forced += workspace.allowed[id] == 0;
    can_spill += input().nodes[id].costs[0] != PBQP_INF;
  }
  size_t count = std::popcount(pending), available = std::popcount(colors);
  if (count <= available || count - available <= forced)
    return true;
  size_t required = count - available;
  if (required > can_spill)
    return false;
  return clique_bound(pending, required, workspace, cost);
}

bool OptimizedBranchAndBoundBounds::lower_bound(
    const size_t *tuple, size_t depth, Workspace &workspace,
    pbqp_cost_t &cost) const noexcept {
  if (!structure.ready())
    return reference.lower_bound(tuple, depth, workspace.baseline, cost);
  if (!prepare_terms(tuple, depth, workspace, cost))
    return false;
  for (Mask clique : structure.cliques())
    if (!strengthen(clique, workspace, cost))
      return false;
  // Feasible edge terms are all zero in this verified shape. They cannot
  // change the numerical unary sum; the final result uses the shared evaluator.
  return true;
}
} // namespace pbqp
