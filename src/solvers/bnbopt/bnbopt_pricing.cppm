/*
 * Bounded heuristic pricing for integer-weight register-capacity relaxations.
 * Double arithmetic chooses auxiliary weights only. Every search capacity is
 * subsequently verified with integer sums over the complete independent sets;
 * pruning costs are accumulated by the original-order floating-point DP.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>
module pbqp:bnbopt_pricing;
import :independent_sets;
namespace pbqp {
enum class PriceResult { Useful, NonPositive, NonFinite };
enum class PricingStatus {
  NotPrepared,
  Enabled,
  IncompleteSets,
  Empty,
  VisitBudget,
  UnsupportedCost,
  NonPositive,
  NonFinite,
  StateBudget,
  ZeroWeights
};
class PriceOptimizer {
  const pbqp_problem &problem;
  const IndependentSets &sets;
  const size_t registers;
  size_t used_iterations = 0;
  std::vector<double> x, y, next, gradient, scores, probabilities;
  double score_patterns(const std::vector<double> &values);
  void prepare_gradient(double temperature);
  void update(double step, double momentum);
  void relax(double temperature);

public:
  static constexpr size_t iterations = 500;
  static constexpr std::array temperatures{4.0, 1.0, 0.25, 0.0625};
  PriceOptimizer(const pbqp_problem &p, const IndependentSets &patterns)
      : problem(p), sets(patterns), registers(p.nodes[0].num_options - 1),
        x(p.num_nodes), y(p.num_nodes), next(p.num_nodes),
        gradient(p.num_nodes), scores(patterns.size()),
        probabilities(scores.size()) {}
  size_t iterations_used() const noexcept { return used_iterations; }
  PriceResult run();
  const std::vector<double> &prices() const noexcept { return x; }
};

double PriceOptimizer::score_patterns(const std::vector<double> &values) {
  double maximum = -std::numeric_limits<double>::infinity();
  size_t k = 0;
  for (const auto &pattern : sets) {
    double score = 0;
    for (size_t id = pattern.first(); id < pattern.size();
         id = pattern.next(id))
      score += values[id];
    scores[k++] = score;
    maximum = std::max(maximum, score);
  }
  return maximum;
}

void PriceOptimizer::prepare_gradient(double temperature) {
  double maximum = score_patterns(y), sum = 0;
  for (size_t k = 0; k < scores.size(); ++k) {
    probabilities[k] = std::exp((scores[k] - maximum) / temperature);
    sum += probabilities[k];
  }
  std::fill(gradient.begin(), gradient.end(), 1.0);
  size_t k = 0;
  for (const auto &pattern : sets) {
    double contribution = registers * probabilities[k++] / sum;
    for (size_t id = pattern.first(); id < pattern.size();
         id = pattern.next(id))
      gradient[id] -= contribution;
  }
}

void PriceOptimizer::update(double step, double momentum) {
  for (size_t id = 0; id < problem.num_nodes; ++id) {
    next[id] = std::clamp(y[id] + step * gradient[id], 0.0,
                          static_cast<double>(problem.nodes[id].costs[0]));
    y[id] = next[id] + momentum * (next[id] - x[id]);
  }
  x.swap(next);
}

void PriceOptimizer::relax(double temperature) {
  y = x;
  double t = 1;
  double step =
      temperature / (static_cast<double>(registers) * sets.maximum_size());
  for (size_t iteration = 0; iteration < iterations; ++iteration) {
    ++used_iterations;
    prepare_gradient(temperature);
    double next_t = (1 + std::sqrt(1 + 4 * t * t)) / 2;
    update(step, (t - 1) / next_t);
    t = next_t;
  }
}

PriceResult PriceOptimizer::run() {
  for (double temperature : temperatures) {
    relax(temperature);
    double maximum = score_patterns(x), total = 0;
    for (double price : x)
      total += price;
    // A nonpositive heuristic score is not an infeasibility certificate.
    // Stop investing in pricing; ordinary and unweighted bounds remain valid.
    if (!std::isfinite(total) || !std::isfinite(maximum))
      return PriceResult::NonFinite;
    if (total <= registers * maximum)
      return PriceResult::NonPositive;
  }
  return PriceResult::Useful;
}

class RegallocPricing {
public:
  static constexpr size_t visit_budget = 8 * 1024 * 1024;
  static constexpr size_t state_budget = 32768;
  static constexpr size_t dp_work_budget = 4 * 1024 * 1024;
  static constexpr size_t maximum_iterations =
      PriceOptimizer::iterations * PriceOptimizer::temperatures.size();
  static constexpr size_t vertex_passes =
      2 * maximum_iterations + PriceOptimizer::temperatures.size();

private:
  std::vector<size_t> weights;
  size_t total = 0, unsupported_node = 0, used_iterations = 0,
         effective_limit = 0;
  double applied_scale = 0;
  PricingStatus state = PricingStatus::NotPrepared;

public:
  void prepare(const pbqp_problem &problem, const IndependentSets &patterns);
  PricingStatus status() const noexcept { return state; }
  size_t unsupported_cost_node() const noexcept { return unsupported_node; }
  size_t iterations_used() const noexcept { return used_iterations; }
  size_t effective_weight_limit() const noexcept { return effective_limit; }
  double scale() const noexcept { return applied_scale; }
  bool ready() const noexcept { return total != 0; }
  size_t total_weight() const noexcept { return total; }
  size_t weight(size_t node) const noexcept { return weights[node]; }
};

void RegallocPricing::prepare(const pbqp_problem &problem,
                              const IndependentSets &patterns) {
  if (ready())
    return;
  if (!patterns.complete()) {
    state = PricingStatus::IncompleteSets;
    return;
  }
  if (!problem.num_nodes || patterns.empty()) {
    state = PricingStatus::Empty;
    return;
  }
  if (patterns.num_entries() > visit_budget / vertex_passes) {
    state = PricingStatus::VisitBudget;
    return;
  }
  for (size_t id = 0; id < problem.num_nodes; ++id) {
    auto spill = problem.nodes[id].costs[0];
    if (!std::isfinite(spill) || spill < 0) {
      unsupported_node = id;
      state = PricingStatus::UnsupportedCost;
      return;
    }
  }
  PriceOptimizer optimizer(problem, patterns);
  auto result = optimizer.run();
  used_iterations = optimizer.iterations_used();
  if (result != PriceResult::Useful) {
    state = result == PriceResult::NonPositive ? PricingStatus::NonPositive
                                               : PricingStatus::NonFinite;
    return;
  }
  double sum = 0;
  for (double price : optimizer.prices())
    sum += price;
  if (!std::isfinite(sum)) {
    state = PricingStatus::NonFinite;
    return;
  }
  if (!(sum > 0)) {
    state = PricingStatus::ZeroWeights;
    return;
  }
  size_t limit = std::min(state_budget, dp_work_budget / problem.num_nodes);
  effective_limit = limit;
  if (!limit) {
    state = PricingStatus::StateBudget;
    return;
  }
  double scale = std::min(4.0, static_cast<double>(limit) / sum);
  applied_scale = scale;
  std::vector<size_t> candidate(problem.num_nodes);
  size_t count = 0;
  for (size_t id = 0; id < candidate.size(); ++id) {
    double value = optimizer.prices()[id] * scale;
    if (!std::isfinite(value) || value < 0) {
      state = PricingStatus::NonFinite;
      return;
    }
    if (value > limit) {
      state = PricingStatus::StateBudget;
      return;
    }
    size_t weight = static_cast<size_t>(value);
    if (weight > limit - count) {
      state = PricingStatus::StateBudget;
      return;
    }
    candidate[id] = weight;
    count += weight;
  }
  if (count) {
    weights = std::move(candidate);
    total = count;
    state = PricingStatus::Enabled;
  } else
    state = PricingStatus::ZeroWeights;
}
} // namespace pbqp
