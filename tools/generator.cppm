/*
 * Public PBQP generation options and vector-backed problem ownership.
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
#include <cstdint>
#include <random>
#include <vector>

export module pbqp.generator;

export namespace pbqp_tools {
// Increment when the documented sampling/consumption protocol changes.
inline constexpr unsigned generation_protocol = 1;

enum class GeneratorFamily { Random, Planted, RegAlloc };

struct GeneratorOptions {
  size_t num_nodes;
  size_t num_edges;
  size_t max_options;
  std::uint64_t seed;
  GeneratorFamily family = GeneratorFamily::Random;
  size_t num_registers = 3;
  pbqp_cost_t spill_cost_min = 10.0f;
  pbqp_cost_t spill_cost_max = 100.0f;
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

/* Random costs use quarter steps in [-10, 10], with 10% INF per entry.
 * Planted mode forces a hidden assignment to be allowed. RegAlloc uses uniform
 * finite spill costs, zero register unary costs, and diagonal INF conflicts.
 * The versioned sampling protocol uses standardized mt19937_64 draws and fixed
 * integer/float conversions, independent of standard-library distributions. */
class ProblemGenerator {
  std::mt19937_64 random;
  pbqp_cost_t next_cost();
  std::vector<pbqp_cost_t> costs(size_t count);

public:
  explicit ProblemGenerator(std::uint64_t seed) : random(seed) {}

  [[nodiscard]] RandomProblem generate(const GeneratorOptions &options);
};

[[nodiscard]] inline RandomProblem
generate_problem(const GeneratorOptions &options) {
  return ProblemGenerator(options.seed).generate(options);
}
} // namespace pbqp_tools
