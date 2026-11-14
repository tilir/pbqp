/*
 * PBQP generator implementation with fixed sampling and owned problem views.
 * Keeps long generation routines and private sampling out of the public module.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <utility>
#include <vector>
module pbqp.generator;
import :sampling;

namespace pbqp_tools {
size_t pair_count(size_t n) {
  size_t a = n, b = n ? n - 1 : 0;
  if (a % 2 == 0)
    a /= 2;
  else
    b /= 2;
  if (b && a > std::numeric_limits<size_t>::max() / b)
    throw std::overflow_error("too many possible edges");
  return a * b;
}

pbqp_cost_t ProblemGenerator::next_cost() {
  if (uniform_index(random, 9) == 0)
    return PBQP_INF;
  return static_cast<pbqp_cost_t>(static_cast<int>(uniform_index(random, 80)) -
                                  40) /
         4.0f;
}

std::vector<pbqp_cost_t> ProblemGenerator::costs(size_t count) {
  std::vector<pbqp_cost_t> result(count);
  // Draw consumption order is part of the portable generation protocol.
  for (auto &cost : result)
    cost = next_cost();
  return result;
}

/* Floyd sampling touches only the smaller of the edge set and its complement.
 * Sorting ranks makes output deterministic regardless of hash table ordering.
 */
std::vector<std::pair<size_t, size_t>> sample_edges(size_t n, size_t m,
                                                    std::mt19937_64 &random) {
  size_t total = pair_count(n);
  if (m > total)
    throw std::invalid_argument("too many edges for this node count");
  bool complement = m > total / 2;
  size_t count = complement ? total - m : m;
  std::unordered_set<size_t> selected;
  selected.reserve(count);
  for (size_t j = total - count; j < total; ++j) {
    size_t rank = uniform_index(random, j);
    if (!selected.insert(rank).second)
      selected.insert(j);
  }
  std::vector<size_t> ranks;
  ranks.reserve(m);
  if (complement) {
    for (size_t rank = 0; rank < total; ++rank)
      if (!selected.contains(rank))
        ranks.push_back(rank);
  } else {
    ranks.assign(selected.begin(), selected.end());
    std::sort(ranks.begin(), ranks.end());
  }
  std::vector<std::pair<size_t, size_t>> result;
  result.reserve(m);
  for (size_t rank : ranks) {
    size_t low = 0, high = n - 1;
    while (low + 1 < high) {
      size_t mid = low + (high - low) / 2;
      if (total - pair_count(n - mid) <= rank)
        low = mid;
      else
        high = mid;
    }
    size_t prefix = total - pair_count(n - low);
    result.emplace_back(low, low + 1 + (rank - prefix));
  }
  return result;
}

RandomProblem ProblemGenerator::generate(const GeneratorOptions &options) {
  const size_t n = options.num_nodes, m = options.num_edges;
  const bool regalloc = options.family == GeneratorFamily::RegAlloc;
  const bool planted = options.family == GeneratorFamily::Planted;
  if (!regalloc && !options.max_options)
    throw std::invalid_argument("MAX_OPTIONS must be positive");
  if (regalloc && (!options.num_registers ||
                   options.num_registers == std::numeric_limits<size_t>::max()))
    throw std::invalid_argument(
        "REGISTERS must be positive and leave room for spill");
  if (regalloc &&
      (!std::isfinite(options.spill_cost_min) ||
       !std::isfinite(options.spill_cost_max) || options.spill_cost_min <= 0 ||
       options.spill_cost_max < options.spill_cost_min))
    throw std::invalid_argument(
        "spill costs must be finite with 0 < min <= max");
  auto endpoints = sample_edges(n, m, random);
  RandomProblem result;
  result.nodes.reserve(n);
  result.node_costs.reserve(n);
  std::vector<size_t> assignment;
  if (planted)
    assignment.reserve(n);
  for (size_t i = 0; i < n; ++i) {
    if (regalloc) {
      result.node_costs.emplace_back(options.num_registers + 1, 0.0f);
      result.node_costs.back()[0] =
          uniform_cost(random, options.spill_cost_min, options.spill_cost_max);
    } else {
      result.node_costs.push_back(
          costs(1 + uniform_index(random, options.max_options - 1)));
      if (planted) {
        auto &values = result.node_costs.back();
        size_t chosen = uniform_index(random, values.size() - 1);
        assignment.push_back(chosen);
        values[chosen] = static_cast<pbqp_cost_t>(
                             static_cast<int>(uniform_index(random, 80)) - 40) /
                         4.0f;
      }
    }
    const auto &values = result.node_costs.back();
    result.nodes.push_back({values.size(), values.data()});
  }
  result.edges.reserve(m);
  result.edge_costs.reserve(regalloc ? std::min(m, size_t{1}) : m);
  for (auto [src, tgt] : endpoints) {
    auto rows = result.nodes[src].num_options,
         cols = result.nodes[tgt].num_options;
    if (rows > std::numeric_limits<size_t>::max() / cols)
      throw std::overflow_error("matrix dimensions overflow");
    if (regalloc) {
      if (result.edge_costs.empty()) {
        result.edge_costs.emplace_back(rows * cols, 0.0f);
        for (size_t r = 1; r < rows; ++r)
          result.edge_costs.back()[r * cols + r] = PBQP_INF;
      }
    } else {
      result.edge_costs.push_back(costs(rows * cols));
      if (planted)
        result.edge_costs.back()[assignment[src] * cols + assignment[tgt]] =
            0.0f;
    }
    result.edges.push_back({src, tgt, result.edge_costs.back().data()});
  }
  return result;
}
} // namespace pbqp_tools
