/*
 * Random PBQP generation interface and implementation.
 *
 * Separates generator settings, random sampling, and ownership of generated
 * data. Exposes a read-only C problem view whose storage belongs to
 * RandomProblem.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;

#include "pbqp.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

export module pbqp.generator;

export namespace pbqp_tools {
struct GeneratorOptions {
  size_t num_nodes;
  size_t num_edges;
  size_t max_options;
  std::uint64_t seed;
};

/* Owns storage for a read-only C view. Copying would invalidate cost pointers.
 * Moving preserves storage addresses. Views expire when the owner is destroyed
 * or assigned another problem. */
class RandomProblem {
  std::vector<pbqp_node> nodes;
  std::vector<pbqp_edge> edges;
  std::vector<std::vector<pbqp_cost_t>> node_costs;
  std::vector<std::vector<pbqp_cost_t>> edge_costs;

  friend class ProblemGenerator;

public:
  RandomProblem() = default;
  RandomProblem(const RandomProblem &) = delete;
  RandomProblem &operator=(const RandomProblem &) = delete;
  RandomProblem(RandomProblem &&) noexcept = default;
  RandomProblem &operator=(RandomProblem &&) noexcept = default;

  [[nodiscard]] pbqp_problem view() const noexcept {
    return {nodes.size(), edges.size(), nodes.data(), edges.data()};
  }
};

/* Finite costs are multiples of 0.25 in [-10, 10]; each entry has a 10%
 * chance of INF.
 * A fresh generator with the same seed reproduces its sequence of problems
 * with the same standard library implementation. */
class ProblemGenerator {
  std::mt19937_64 random;
  pbqp_cost_t next_cost();
  std::vector<pbqp_cost_t> costs(size_t count);

public:
  explicit ProblemGenerator(std::uint64_t seed) : random(seed) {}
  [[nodiscard]] RandomProblem generate(size_t num_nodes, size_t num_edges,
                                       size_t max_options);
};

[[nodiscard]] inline RandomProblem
generate_problem(const GeneratorOptions &options) {
  return ProblemGenerator(options.seed)
      .generate(options.num_nodes, options.num_edges, options.max_options);
}
} // namespace pbqp_tools

namespace pbqp_tools {
namespace {
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
} // namespace

pbqp_cost_t ProblemGenerator::next_cost() {
  if (std::uniform_int_distribution<int>(0, 9)(random) == 0)
    return PBQP_INF;
  return static_cast<pbqp_cost_t>(
             std::uniform_int_distribution<int>(-40, 40)(random)) /
         4.0f;
}

std::vector<pbqp_cost_t> ProblemGenerator::costs(size_t count) {
  std::vector<pbqp_cost_t> result(count);
  std::generate(result.begin(), result.end(), [this] { return next_cost(); });
  return result;
}

RandomProblem ProblemGenerator::generate(size_t n, size_t m,
                                         size_t max_options) {
  if (!max_options)
    throw std::invalid_argument("MAX_OPTIONS must be positive");
  size_t remaining_pairs = pair_count(n);
  if (m > remaining_pairs)
    throw std::invalid_argument("too many edges for this node count");

  RandomProblem result;
  result.nodes.reserve(n);
  result.node_costs.reserve(n);
  std::uniform_int_distribution<size_t> dimensions(1, max_options);
  for (size_t i = 0; i < n; ++i) {
    result.node_costs.push_back(costs(dimensions(random)));
    const auto &values = result.node_costs.back();
    result.nodes.push_back({values.size(), values.data()});
  }

  result.edges.reserve(m);
  result.edge_costs.reserve(m);
  size_t remaining_edges = m;
  for (size_t src = 0; src < n && remaining_edges; ++src) {
    for (size_t tgt = src + 1; tgt < n && remaining_edges; ++tgt) {
      bool selected = std::uniform_int_distribution<size_t>(
                          0, remaining_pairs - 1)(random) < remaining_edges;
      --remaining_pairs;
      if (!selected)
        continue;
      --remaining_edges;
      auto rows = result.nodes[src].num_options,
           cols = result.nodes[tgt].num_options;
      if (rows > std::numeric_limits<size_t>::max() / cols)
        throw std::overflow_error("matrix dimensions overflow");
      result.edge_costs.push_back(costs(rows * cols));
      result.edges.push_back({src, tgt, result.edge_costs.back().data()});
    }
  }
  return result;
}
} // namespace pbqp_tools
