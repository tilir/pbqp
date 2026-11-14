/*
 * Bounded primal coverage seed for verified register-interference problems.
 *
 * Choose at most R maximal independent sets and assign each covered node to
 * its first chosen register class. Greedy restarts, one/two-class exchanges,
 * and bounded ruin-and-recreate use double gains only for ranking. Returned
 * costs use the shared float evaluator.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <random>
#include <vector>
module pbqp:bnbopt_cover;
import :independent_sets;
import :bitset;
import :ownership;
import :evaluation;
namespace pbqp {
class RegisterCoverSeed {
public:
  static constexpr size_t work_budget = 8 * 1024 * 1024;
  static constexpr size_t restart_limit = 128;
  static constexpr size_t exchange_passes = 32;
  static constexpr size_t pair_candidates = 16;
  static constexpr size_t pair_passes = 4;
  static constexpr size_t fresh_period = 4;
  static constexpr size_t minimum_ruin = 2;
  static constexpr size_t ruin_sizes = 3;

private:
  const pbqp_problem &problem;
  size_t colors, patterns, work = 0, restarts = 0;
  bool supported = true, found = false;
  std::vector<size_t> offsets, members, chosen, covered, options, best_chosen,
      order;
  std::vector<double> costs, values, gains;
  std::vector<const BitSet<> *> masks;
  SolutionPtr best;
  std::mt19937 random{1};

  bool choose(const std::vector<double> &weights, size_t &selected,
              double &gain) noexcept;
  void cover(size_t pattern, bool add) noexcept;
  bool greedy() noexcept;
  bool recreate(size_t attempt) noexcept;
  bool improve() noexcept;
  bool rank_pairs(std::array<size_t, pair_candidates> &ranked,
                  size_t &count) noexcept;
  bool pair_gain(size_t first, size_t second, double &gain) noexcept;
  bool improve_pairs() noexcept;
  bool keep() noexcept;

public:
  RegisterCoverSeed(const pbqp_problem &input, const IndependentSets &sets);
  ~RegisterCoverSeed() noexcept;
  bool run(pbqp_cost_t target = 0) noexcept;
  bool enabled() const noexcept { return supported; }
  size_t work_used() const noexcept { return work; }
  size_t restarts_used() const noexcept { return restarts; }
  const pbqp_solution &solution() const noexcept { return *best; }
};

RegisterCoverSeed::RegisterCoverSeed(const pbqp_problem &input,
                                     const IndependentSets &sets)
    : problem(input),
      colors(input.num_nodes
                 ? std::min(input.num_nodes, input.nodes[0].num_options - 1)
                 : 0),
      patterns(sets.size()), offsets(patterns + 1), members(sets.num_entries()),
      chosen(colors), covered(input.num_nodes), options(input.num_nodes),
      best_chosen(colors), order(colors), costs(input.num_nodes),
      values(input.num_nodes), gains(patterns), masks(patterns),
      best(make_solution(input.num_nodes)) {
  supported = colors && patterns && sets.complete();
  std::iota(order.begin(), order.end(), 0);
  for (size_t id = 0; id < input.num_nodes; ++id) {
    costs[id] = input.nodes[id].costs[0];
    supported &= std::isfinite(costs[id]) && costs[id] >= 0;
  }
  size_t index = 0, count = 0;
  for (const auto &pattern : sets) {
    masks[index] = &pattern;
    offsets[index++] = count;
    for (size_t id = pattern.first(); id < pattern.size();
         id = pattern.next(id))
      members[count++] = id;
  }
  offsets[index] = count;
}

RegisterCoverSeed::~RegisterCoverSeed() noexcept = default;

bool RegisterCoverSeed::choose(const std::vector<double> &weights,
                               size_t &selected, double &gain) noexcept {
  gain = -1;
  selected = 0;
  for (size_t pattern = 0; pattern < patterns; ++pattern) {
    const size_t count = offsets[pattern + 1] - offsets[pattern];
    if (count > work_budget - work)
      return false;
    work += count;
    double score = 0;
    for (size_t index = offsets[pattern]; index < offsets[pattern + 1];
         ++index) {
      const size_t id = members[index];
      if (!covered[id])
        score += weights[id];
    }
    if (score > gain) {
      selected = pattern;
      gain = score;
    }
  }
  return true;
}

void RegisterCoverSeed::cover(size_t pattern, bool add) noexcept {
  for (size_t index = offsets[pattern]; index < offsets[pattern + 1]; ++index) {
    auto &count = covered[members[index]];
    if (add)
      ++count;
    else
      --count;
  }
}

bool RegisterCoverSeed::greedy() noexcept {
  std::fill(covered.begin(), covered.end(), 0);
  for (size_t color = 0; color < colors; ++color) {
    double gain;
    if (!choose(values, chosen[color], gain))
      return false;
    cover(chosen[color], true);
  }
  return true;
}

bool RegisterCoverSeed::recreate(size_t attempt) noexcept {
  std::copy(best_chosen.begin(), best_chosen.end(), chosen.begin());
  std::fill(covered.begin(), covered.end(), 0);
  for (size_t pattern : chosen)
    cover(pattern, true);
  std::shuffle(order.begin(), order.end(), random);
  const size_t count = std::min(colors, minimum_ruin + attempt % ruin_sizes);
  for (size_t i = 0; i < count; ++i)
    cover(chosen[order[i]], false);
  for (size_t i = 0; i < count; ++i) {
    double gain;
    if (!choose(values, chosen[order[i]], gain))
      return false;
    cover(chosen[order[i]], true);
  }
  return true;
}

bool RegisterCoverSeed::improve() noexcept {
  for (size_t pass = 0; pass < exchange_passes; ++pass) {
    bool changed = false;
    for (size_t color = 0; color < colors; ++color) {
      const size_t original = chosen[color];
      cover(original, false);
      double previous = 0;
      for (size_t index = offsets[original]; index < offsets[original + 1];
           ++index)
        if (!covered[members[index]])
          previous += costs[members[index]];
      size_t candidate;
      double gain;
      if (!choose(costs, candidate, gain)) {
        cover(original, true);
        return false;
      }
      if (gain > previous) {
        chosen[color] = candidate;
        changed = true;
      }
      cover(chosen[color], true);
    }
    if (!changed)
      break;
  }
  return true;
}

bool RegisterCoverSeed::rank_pairs(std::array<size_t, pair_candidates> &ranked,
                                   size_t &count) noexcept {
  count = 0;
  for (size_t pattern = 0; pattern < patterns; ++pattern) {
    const size_t length = offsets[pattern + 1] - offsets[pattern];
    if (length > work_budget - work)
      return false;
    work += length;
    double gain = 0;
    for (size_t i = offsets[pattern]; i < offsets[pattern + 1]; ++i)
      if (!covered[members[i]])
        gain += costs[members[i]];
    gains[pattern] = gain;
    size_t position = count;
    if (count == pair_candidates) {
      if (gain <= gains[ranked[count - 1]])
        continue;
      --position;
    } else
      ++count;
    while (position && gain > gains[ranked[position - 1]]) {
      ranked[position] = ranked[position - 1];
      --position;
    }
    ranked[position] = pattern;
  }
  return true;
}

bool RegisterCoverSeed::pair_gain(size_t first, size_t second,
                                  double &gain) noexcept {
  if (offsets[first + 1] - offsets[first] >
      offsets[second + 1] - offsets[second])
    std::swap(first, second);
  const size_t length = offsets[first + 1] - offsets[first];
  if (length > work_budget - work)
    return false;
  work += length;
  gain = gains[first] + gains[second];
  for (size_t i = offsets[first]; i < offsets[first + 1]; ++i) {
    const size_t id = members[i];
    if (!covered[id] && masks[second]->test(id))
      gain -= costs[id];
  }
  return true;
}

bool RegisterCoverSeed::improve_pairs() noexcept {
  for (size_t pass = 0; pass < pair_passes; ++pass) {
    bool changed = false;
    for (size_t a = 0; a < colors; ++a)
      for (size_t b = a + 1; b < colors; ++b) {
        const size_t old_a = chosen[a], old_b = chosen[b];
        cover(old_a, false);
        cover(old_b, false);
        auto restore = [&] {
          cover(chosen[a], true);
          cover(chosen[b], true);
        };
        std::array<size_t, pair_candidates> ranked{};
        size_t count;
        double best_gain;
        if (!rank_pairs(ranked, count) || !pair_gain(old_a, old_b, best_gain)) {
          restore();
          return false;
        }
        size_t best_a = old_a, best_b = old_b;
        for (size_t i = 0; i < count; ++i)
          for (size_t j = i + 1; j < count; ++j) {
            double gain;
            if (!pair_gain(ranked[i], ranked[j], gain)) {
              restore();
              return false;
            }
            if (gain > best_gain) {
              best_gain = gain;
              best_a = ranked[i];
              best_b = ranked[j];
            }
          }
        chosen[a] = best_a;
        chosen[b] = best_b;
        changed |= best_a != old_a || best_b != old_b;
        restore();
      }
    if (!changed)
      break;
  }
  return true;
}

bool RegisterCoverSeed::keep() noexcept {
  std::fill(options.begin(), options.end(), 0);
  for (size_t color = 0; color < colors; ++color)
    for (size_t index = offsets[chosen[color]];
         index < offsets[chosen[color] + 1]; ++index)
      if (!options[members[index]])
        options[members[index]] = color + 1;
  pbqp_cost_t cost;
  if (!evaluate_assignment(problem, options.data(), cost))
    return false;
  if (!found || cost < best->cost) {
    found = true;
    best->cost = cost;
    best->quality = PBQP_SOLUTION_FEASIBLE;
    std::copy(options.begin(), options.end(), best->options);
    std::copy(chosen.begin(), chosen.end(), best_chosen.begin());
    return true;
  }
  return false;
}

bool RegisterCoverSeed::run(pbqp_cost_t target) noexcept {
  if (!supported)
    return false;
  std::uniform_real_distribution<double> perturbation(0.25, 1.75);
  for (size_t attempt = 0; attempt < restart_limit; ++attempt) {
    ++restarts;
    for (size_t id = 0; id < problem.num_nodes; ++id)
      values[id] = costs[id] * (attempt ? perturbation(random) : 1.0);
    const bool fresh = !found || attempt % fresh_period == 0;
    const bool built = fresh ? greedy() : recreate(attempt);
    if (!built)
      break;
    const bool remaining = improve();
    const bool improved = keep();
    if (found && best->cost <= target)
      break;
    if (improved && remaining) {
      const bool pairs_remaining = improve_pairs();
      keep();
      if (found && best->cost <= target)
        break;
      if (!pairs_remaining)
        break;
    }
    if (!remaining)
      break;
  }
  return found;
}
} // namespace pbqp
