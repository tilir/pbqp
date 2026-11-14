/*
 * Optimized exact bounds with clique capacity and reference-order spill DP.
 *
 * Verified regalloc inputs use dynamic-bitset forward checking, structural
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
#include <cmath>
#include <vector>

module pbqp:bnbopt_bounds;
import :bitset;
import :bnb_bounds;
import :bnbopt_structure;
import :independent_sets;
import :bnbopt_pricing;
import :bnbopt_prefix_pricing;
import :diagnostics;
import :bnbopt_diagnostics;

namespace pbqp {
class OptimizedBranchAndBoundBounds {
  BranchAndBoundBounds reference;
  RegallocStructure structure;
  IndependentSets independent;
  RegallocPricing pricing;
  PrefixPricingPlan prefix_plan;

public:
  struct Workspace {
    BranchAndBoundBounds::Workspace baseline;
    std::vector<BitSet<>> colored, allowed;
    std::vector<pbqp_cost_t> terms, current, next;
    std::vector<unsigned char> current_live, next_live, has_register;
    BitSet<> assigned, pending, colors;
    PrefixPriceOptimizer prefix;
    std::vector<size_t> priced_options;
    size_t priced_depth = 0;
    pbqp_cost_t cutoff = PBQP_INF, priced_cutoff = PBQP_INF;
    bool using_prefix_prices = false, has_priced = false,
         dynamic_prefix = false;
    explicit Workspace(const OptimizedBranchAndBoundBounds &bounds)
        : baseline(bounds.reference),
          colored(std::min(bounds.workspace_nodes(), bounds.registers()),
                  BitSet<>(bounds.workspace_nodes())),
          allowed(bounds.workspace_nodes(), BitSet<>(bounds.registers())),
          terms(bounds.workspace_nodes()), current(bounds.state_count()),
          next(bounds.state_count()), current_live(bounds.state_count()),
          next_live(bounds.state_count()),
          has_register(bounds.workspace_nodes()),
          assigned(bounds.workspace_nodes()), pending(bounds.workspace_nodes()),
          colors(bounds.registers()),
          prefix(bounds.input(), bounds.independent, bounds.pricing,
                 bounds.registers(), bounds.prefix_plan),
          priced_options(prefix.enabled() ? bounds.input().num_nodes : 0) {}
  };

private:
  size_t state_count() const noexcept {
    return 1 + std::max(workspace_nodes(), std::max(pricing.total_weight(),
                                                    prefix_plan.state_limit));
  }
  bool refine_prepared(const size_t *tuple, size_t depth, Workspace &workspace,
                       pbqp_cost_t &cost) const noexcept;
  size_t capacity_weight(bool weighted, const Workspace &workspace,
                         size_t id) const noexcept {
    if (!weighted)
      return 1;
    return workspace.using_prefix_prices ? workspace.prefix.weight(id)
                                         : pricing.weight(id);
  }
  bool capacity_bound(bool weighted, Workspace &workspace,
                      pbqp_cost_t &cost) const noexcept;
  size_t workspace_nodes() const noexcept {
    return structure.eligible() ? input().num_nodes : 0;
  }
  size_t registers() const noexcept {
    return structure.eligible() ? input().nodes[0].num_options - 1 : 0;
  }
  bool prepare_terms(const size_t *tuple, size_t depth, Workspace &workspace,
                     pbqp_cost_t &cost) const noexcept;
  bool spill_bound(const BitSet<> &pending, size_t required, bool weighted,
                   Workspace &workspace, pbqp_cost_t &cost) const noexcept;
  bool strengthen(const BitSet<> &clique, Workspace &workspace,
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
      : reference(p), structure(p, reference.is_pure_interference_problem()) {}
  const pbqp_problem &input() const noexcept { return reference.input(); }
  const pbqp_problem &domains() const noexcept { return structure.domains(); }
  size_t node(size_t step) const noexcept { return structure.node(step); }
  size_t option(size_t node, size_t rank) const noexcept {
    return reference.option(node, rank);
  }
  void describe_input(Diagnostics log) const {
    describe_bnbopt_input(input(), reference.interference_check(), structure,
                          log);
  }
  void describe_structure(Diagnostics log) const {
    describe_bnbopt_structure(structure, log);
  }
  void prepare_search() { structure.prepare(); }
  void prepare_capacity(Diagnostics log = {}) {
    if (!structure.ready())
      return;
    independent.prepare(structure.adjacency());
    pricing.prepare(input(), independent);
    prefix_plan =
        plan_prefix_pricing(input(), independent, pricing, registers());
    describe_bnbopt_capacity(input(), independent, pricing, log);
    describe_bnbopt_prefix_pricing(prefix_plan, log);
  }
  const PrefixPricingPlan &prefix_pricing_plan() const noexcept {
    return prefix_plan;
  }
  const IndependentSets &patterns() const noexcept { return independent; }
  const RegallocPricing &weights() const noexcept { return pricing; }
  bool optimized() const noexcept { return structure.ready(); }
  const RegallocStructure &graph() const noexcept { return structure; }
  bool lower_bound(const size_t *tuple, size_t depth, Workspace &workspace,
                   pbqp_cost_t &cost) const noexcept;
  void prepare_prefix(const size_t *tuple, size_t depth, Workspace &workspace,
                      pbqp_cost_t cutoff) const noexcept;
};

bool OptimizedBranchAndBoundBounds::prepare_terms(
    const size_t *tuple, size_t depth, Workspace &workspace,
    pbqp_cost_t &cost) const noexcept {
  for (auto &color : workspace.colored)
    color.clear();
  workspace.assigned.clear();
  size_t used = 0;
  for (size_t step = 0; step < depth; ++step) {
    size_t id = node(step), option = tuple[id];
    if (option > used + 1 || input().nodes[id].costs[option] == PBQP_INF)
      return false;
    if (option) {
      if (structure.neighbors(id).intersects(workspace.colored[option - 1]))
        return false;
      workspace.colored[option - 1].set(id);
      used = std::max(used, option);
    }
    workspace.assigned.set(id);
  }
  cost = 0;
  for (size_t id = 0; id < input().num_nodes; ++id) {
    pbqp_cost_t term;
    if (workspace.assigned.test(id)) {
      term = input().nodes[id].costs[tuple[id]];
      workspace.allowed[id].clear();
      workspace.has_register[id] = false;
    } else {
      auto &allowed = workspace.allowed[id];
      allowed.fill();
      for (size_t color = 0; color < used; ++color)
        if (structure.neighbors(id).intersects(workspace.colored[color]))
          allowed.reset(color);
      auto spill = input().nodes[id].costs[0];
      workspace.has_register[id] = allowed.any();
      term =
          workspace.has_register[id] ? std::min(spill, pbqp_cost_t{0}) : spill;
      if (term == PBQP_INF)
        return false;
    }
    workspace.terms[id] = term;
    cost = pbqp_cost_add(cost, term);
  }
  return true;
}

bool OptimizedBranchAndBoundBounds::spill_bound(
    const BitSet<> &pending, size_t required, bool weighted,
    Workspace &workspace, pbqp_cost_t &cost) const noexcept {
  // +INF is a valid reachable rounded score. Liveness cannot use a sentinel.
  std::fill_n(workspace.current_live.begin(), required + 1, false);
  workspace.current_live[0] = true;
  workspace.current[0] = 0;
  // State 'required' also represents larger spill counts. Future choices in
  // this relaxation depend only on the state, so the cheapest prefix suffices.
  for (size_t id = 0; id < input().num_nodes; ++id) {
    if (!pending.test(id)) {
      for (size_t state = 0; state <= required; ++state)
        if (workspace.current_live[state])
          workspace.current[state] =
              pbqp_cost_add(workspace.current[state], workspace.terms[id]);
      continue;
    }
    std::fill_n(workspace.next_live.begin(), required + 1, false);
    auto spill = input().nodes[id].costs[0];
    bool has_register = workspace.has_register[id];
    size_t weight = capacity_weight(weighted, workspace, id);
    for (size_t state = 0; state <= required; ++state) {
      if (!workspace.current_live[state])
        continue;
      if (has_register)
        update(workspace, state, pbqp_cost_add(workspace.current[state], 0));
      if (spill != PBQP_INF)
        update(workspace,
               weight >= required - state ? required : state + weight,
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
    const BitSet<> &clique, Workspace &workspace,
    pbqp_cost_t &cost) const noexcept {
  auto &pending = workspace.pending;
  pending.assign_difference(clique, workspace.assigned);
  auto &colors = workspace.colors;
  colors.clear();
  size_t forced = 0, can_spill = 0;
  for (size_t id = pending.first(); id < pending.size();
       id = pending.next(id)) {
    colors.unite_with(workspace.allowed[id]);
    forced += !workspace.has_register[id];
    can_spill += input().nodes[id].costs[0] != PBQP_INF;
  }
  size_t count = pending.count(), available = colors.count();
  if (count <= available || count - available <= forced)
    return true;
  size_t required = count - available;
  if (required > can_spill)
    return false;
  return spill_bound(pending, required, false, workspace, cost);
}

bool OptimizedBranchAndBoundBounds::capacity_bound(
    bool weighted, Workspace &workspace, pbqp_cost_t &cost) const noexcept {
  auto &pending = workspace.pending;
  pending.fill();
  pending.subtract(workspace.assigned);
  size_t total = 0, capacity = 0;
  for (size_t id = pending.first(); id < pending.size(); id = pending.next(id))
    total += capacity_weight(weighted, workspace, id);
  if (!total)
    return true;
  for (size_t color = 0; color < registers(); ++color) {
    size_t best = 0;
    // Every independent extension lies in a maximal independent superset.
    // Intersecting with allowed pending nodes preserves that extension, and
    // nonnegative unit/auxiliary weights make its capacity no smaller.
    for (const auto &pattern : independent) {
      if (color < workspace.colored.size() &&
          !workspace.colored[color].is_subset_of(pattern))
        continue;
      size_t available = 0;
      for (size_t id = pattern.first(); id < pattern.size();
           id = pattern.next(id))
        if (pending.test(id) && workspace.allowed[id].test(color))
          available += capacity_weight(weighted, workspace, id);
      best = std::max(best, available);
    }
    if (best >= total - capacity)
      return true;
    capacity += best;
  }
  return spill_bound(pending, total - capacity, weighted, workspace, cost);
}

bool OptimizedBranchAndBoundBounds::lower_bound(
    const size_t *tuple, size_t depth, Workspace &workspace,
    pbqp_cost_t &cost) const noexcept {
  if (!structure.ready())
    return reference.lower_bound(tuple, depth, workspace.baseline, cost);
  if (!prepare_terms(tuple, depth, workspace, cost))
    return false;
  if (workspace.prefix.available()) {
    workspace.using_prefix_prices = true;
    const bool possible = capacity_bound(true, workspace, cost);
    workspace.using_prefix_prices = false;
    if (!possible)
      return false;
    if (std::isfinite(workspace.cutoff) && cost >= workspace.cutoff)
      return true;
  }
  if (independent.complete()) {
    if (pricing.ready() && !capacity_bound(true, workspace, cost))
      return false;
    if (!capacity_bound(false, workspace, cost))
      return false;
  }
  for (const auto &clique : structure.cliques())
    if (!strengthen(clique, workspace, cost))
      return false;
  // Feasible edge terms are all zero in this verified shape. They cannot
  // change the numerical unary sum; the final result uses the shared evaluator.
  if (workspace.dynamic_prefix && cost < workspace.cutoff)
    return refine_prepared(tuple, depth, workspace, cost);
  return true;
}
bool OptimizedBranchAndBoundBounds::refine_prepared(
    const size_t *tuple, size_t depth, Workspace &workspace,
    pbqp_cost_t &cost) const noexcept {
  const auto cutoff = workspace.cutoff;
  auto &optimizer = workspace.prefix;
  if (!optimizer.enabled() || !std::isfinite(cutoff) ||
      depth == input().num_nodes || depth < structure.cliques().front().count())
    return true;
  if (workspace.has_priced && depth >= workspace.priced_depth &&
      depth - workspace.priced_depth < PrefixPriceOptimizer::refresh_depth &&
      cutoff >= workspace.priced_cutoff) {
    bool same = true;
    for (size_t step = 0; step < workspace.priced_depth; ++step)
      same &= tuple[node(step)] == workspace.priced_options[step];
    if (same)
      return true;
  }
  if (!optimizer.begin(workspace.colored, workspace.assigned))
    return true;
  workspace.priced_depth = depth;
  workspace.priced_cutoff = cutoff;
  workspace.has_priced = true;
  for (size_t step = 0; step < depth; ++step)
    workspace.priced_options[step] = tuple[node(step)];
  for (double temperature : PriceOptimizer::temperatures) {
    if (!optimizer.relax(temperature))
      break;
    if (!optimizer.quantize())
      continue;
    // The real dual score never prunes. Test the committed integer weights
    // with the original-order DP, including fixed spill and overflow terms.
    pbqp_cost_t candidate = 0;
    for (pbqp_cost_t term : workspace.terms)
      candidate = pbqp_cost_add(candidate, term);
    workspace.using_prefix_prices = true;
    const bool possible = capacity_bound(true, workspace, candidate);
    workspace.using_prefix_prices = false;
    if (!possible)
      return false;
    cost = std::max(cost, candidate);
    if (cost >= cutoff)
      break;
  }
  return true;
}

void OptimizedBranchAndBoundBounds::prepare_prefix(
    const size_t *tuple, size_t depth, Workspace &workspace,
    pbqp_cost_t cutoff) const noexcept {
  if (!workspace.prefix.enabled())
    return;
  workspace.dynamic_prefix = true;
  workspace.cutoff = cutoff;
  pbqp_cost_t bound;
  lower_bound(tuple, depth, workspace, bound);
}

} // namespace pbqp
