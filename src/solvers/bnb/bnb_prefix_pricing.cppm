/*
 * Allocation-free, prefix-conditioned softmax pricing for worker-local bounds.
 *
 * Each used register has its own compatible maximal-set group. Unused
 * registers share one group with a multiplicity. Prices remain heuristic:
 * only quantized integer capacities and reference-order cost DP may prune.
 * All scratch storage is allocated before search; incomplete pricing is safe.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <span>
#include <vector>
module pbqp:bnb_prefix_pricing;
import :bitset;
import :independent_sets;
import :bnb_pricing;

namespace pbqp {
enum class PrefixPricingStatus { RootUnavailable, CandidateBudget, Enabled };

struct PrefixPricingPlan {
  PrefixPricingStatus status = PrefixPricingStatus::RootUnavailable;
  size_t groups = 0, candidates = 0, state_limit = 0;
};

PrefixPricingPlan plan_prefix_pricing(const pbqp_problem &problem,
                                      const IndependentSets &sets,
                                      const RegallocPricing &root,
                                      size_t registers) noexcept;

class PrefixPriceOptimizer {
public:
  static constexpr size_t candidate_budget = 65536;
  static constexpr size_t work_budget = 2 * 1024 * 1024;
  static constexpr size_t iterations = 64;
  static constexpr size_t refresh_depth = 4;

private:
  const pbqp_problem &problem;
  const IndependentSets &sets;
  const RegallocPricing &root;
  size_t registers, limit = 0, groups = 0, alpha = 0;
  size_t remaining = 0, iteration_work = 0, completed_iterations = 0;
  size_t selected_total = 0;
  std::vector<double> x, y, next, gradient, scores, probabilities;
  std::vector<size_t> offsets, members, group_offsets, candidates, multiplicity;
  std::vector<size_t> candidate_weights, selected_weights;
  std::vector<unsigned char> fixed;

  void reduce_patterns(const BitSet<> &assigned) noexcept;
  void build_groups(std::span<const BitSet<>> colored) noexcept;
  void prepare_gradient(double temperature) noexcept;

public:
  PrefixPriceOptimizer(const pbqp_problem &p, const IndependentSets &patterns,
                       const RegallocPricing &pricing, size_t colors,
                       const PrefixPricingPlan &plan);
  PrefixPriceOptimizer(PrefixPriceOptimizer &&) noexcept = default;
  PrefixPriceOptimizer(const PrefixPriceOptimizer &) = delete;
  ~PrefixPriceOptimizer() noexcept;

  bool enabled() const noexcept { return limit != 0; }

  size_t state_limit() const noexcept { return limit; }

  bool available() const noexcept { return selected_total != 0; }

  size_t weight(size_t id) const noexcept { return selected_weights[id]; }

  size_t total_weight() const noexcept { return selected_total; }

  size_t iterations_used() const noexcept { return completed_iterations; }

  bool begin(std::span<const BitSet<>> colored,
             const BitSet<> &assigned) noexcept;
  bool relax(double temperature) noexcept;
  bool quantize() noexcept;
};

PrefixPricingPlan plan_prefix_pricing(const pbqp_problem &problem,
                                      const IndependentSets &sets,
                                      const RegallocPricing &root,
                                      size_t registers) noexcept {
  if (!root.ready() || sets.empty())
    return {};
  const size_t max_groups = std::min(problem.num_nodes, registers);
  if (!max_groups)
    return {};
  if (sets.size() > PrefixPriceOptimizer::candidate_budget / max_groups)
    return {PrefixPricingStatus::CandidateBudget, max_groups, 0, 0};
  const size_t cap = root.effective_weight_limit();
  size_t total = 0;
  for (size_t id = 0; id < problem.num_nodes; ++id) {
    // p <= spill and scale <= 4 bound every possible rounded auxiliary weight.
    // Stop at the existing state budget before any integer conversion can grow.
    const double maximum = std::floor(4.0 * problem.nodes[id].costs[0] + 0.5);
    if (maximum >= static_cast<double>(cap - total))
      return {PrefixPricingStatus::Enabled, max_groups,
              max_groups * sets.size(), cap};
    total += static_cast<size_t>(maximum);
  }
  return {PrefixPricingStatus::Enabled, max_groups, max_groups * sets.size(),
          total};
}

PrefixPriceOptimizer::PrefixPriceOptimizer(const pbqp_problem &p,
                                           const IndependentSets &patterns,
                                           const RegallocPricing &pricing,
                                           size_t colors,
                                           const PrefixPricingPlan &plan)
    : problem(p), sets(patterns), root(pricing), registers(colors),
      limit(plan.state_limit) {
  if (!limit)
    return;
  const size_t nodes = problem.num_nodes;
  const size_t max_groups = plan.groups;
  const size_t slots = plan.candidates;
  x.resize(nodes);
  y.resize(nodes);
  next.resize(nodes);
  gradient.resize(nodes);
  scores.resize(slots);
  probabilities.resize(slots);
  offsets.resize(sets.size() + 1);
  members.resize(sets.num_entries());
  group_offsets.resize(max_groups + 1);
  candidates.resize(slots);
  multiplicity.resize(max_groups);
  candidate_weights.resize(nodes);
  selected_weights.resize(nodes);
  fixed.resize(nodes);
}

PrefixPriceOptimizer::~PrefixPriceOptimizer() noexcept = default;

void PrefixPriceOptimizer::reduce_patterns(const BitSet<> &assigned) noexcept {
  size_t index = 0, used = 0;
  alpha = 0;
  for (const auto &pattern : sets) {
    offsets[index++] = used;
    const size_t first = used;
    for (size_t id : pattern)
      if (!assigned.test(id))
        members[used++] = id;
    alpha = std::max(alpha, used - first);
  }
  offsets[index] = used;
}

void PrefixPriceOptimizer::build_groups(
    std::span<const BitSet<>> colored) noexcept {
  size_t used_colors = 0, count = 0;
  groups = iteration_work = 0;
  for (const auto &color : colored) {
    if (!color.any())
      break;
    ++used_colors;
    group_offsets[groups] = count;
    multiplicity[groups++] = 1;
    size_t index = 0;
    for (const auto &pattern : sets) {
      if (color.is_subset_of(pattern)) {
        candidates[count++] = index;
        // Charge scoring and gradient passes, including per-candidate work.
        iteration_work += 2 * (1 + offsets[index + 1] - offsets[index]);
      }
      ++index;
    }
  }
  if (used_colors < registers) {
    group_offsets[groups] = count;
    multiplicity[groups++] = registers - used_colors;
    for (size_t index = 0; index < sets.size(); ++index) {
      candidates[count++] = index;
      iteration_work += 2 * (1 + offsets[index + 1] - offsets[index]);
    }
  }
  group_offsets[groups] = count;
}

bool PrefixPriceOptimizer::begin(std::span<const BitSet<>> colored,
                                 const BitSet<> &assigned) noexcept {
  if (!enabled())
    return false;
  reduce_patterns(assigned);
  if (!alpha)
    return false;
  build_groups(colored);
  if (!groups || !iteration_work)
    return false;
  for (size_t id = 0; id < problem.num_nodes; ++id) {
    fixed[id] = assigned.test(id);
    x[id] = fixed[id]
                ? 0.0
                : std::min(static_cast<double>(problem.nodes[id].costs[0]),
                           root.weight(id) / root.scale());
  }
  remaining = work_budget;
  completed_iterations = 0;
  return true;
}

void PrefixPriceOptimizer::prepare_gradient(double temperature) noexcept {
  std::fill(gradient.begin(), gradient.end(), 1.0);
  for (size_t group = 0; group < groups; ++group) {
    const size_t first = group_offsets[group], end = group_offsets[group + 1];
    double maximum = 0;
    for (size_t k = first; k < end; ++k) {
      const size_t pattern = candidates[k];
      double score = 0;
      for (size_t i = offsets[pattern]; i < offsets[pattern + 1]; ++i)
        score += y[members[i]];
      scores[k] = score;
      if (k == first || score > maximum)
        maximum = score;
    }
    double total = 0;
    for (size_t k = first; k < end; ++k) {
      probabilities[k] = std::exp((scores[k] - maximum) / temperature);
      total += probabilities[k];
    }
    for (size_t k = first; k < end; ++k) {
      const size_t pattern = candidates[k];
      const double contribution =
          multiplicity[group] * probabilities[k] / total;
      for (size_t i = offsets[pattern]; i < offsets[pattern + 1]; ++i)
        gradient[members[i]] -= contribution;
    }
  }
}

bool PrefixPriceOptimizer::relax(double temperature) noexcept {
  std::copy(x.begin(), x.end(), y.begin());
  double t = 1;
  const double step = temperature / (static_cast<double>(registers) * alpha);
  size_t performed = 0;
  for (; performed < iterations && iteration_work <= remaining; ++performed) {
    remaining -= iteration_work;
    prepare_gradient(temperature);
    const double next_t = (1 + std::sqrt(1 + 4 * t * t)) / 2;
    const double momentum = (t - 1) / next_t;
    for (size_t id = 0; id < problem.num_nodes; ++id) {
      // Assigned vertices were removed from every pattern; keep their prices
      // zero even though their unused gradient entries retain the value one.
      next[id] =
          fixed[id]
              ? 0.0
              : std::clamp(y[id] + step * gradient[id], 0.0,
                           static_cast<double>(problem.nodes[id].costs[0]));
      y[id] = next[id] + momentum * (next[id] - x[id]);
    }
    x.swap(next);
    t = next_t;
  }
  completed_iterations += performed;
  return performed != 0;
}

bool PrefixPriceOptimizer::quantize() noexcept {
  // The global conversion budget reserves rounding headroom for every node.
  // The tighter workspace bound is only a storage bound, not that budget:
  // many zero-cost vertices can make it smaller than the headroom itself.
  const auto result =
      quantize_prices_into(x, root.effective_weight_limit(), candidate_weights);
  if (result.status != PricingStatus::Enabled)
    return false;
  assert(result.total <= limit); // p <= c and scale <= 4 bound storage.
  std::copy(candidate_weights.begin(), candidate_weights.end(),
            selected_weights.begin());
  selected_total = result.total;
  return true;
}
} // namespace pbqp
