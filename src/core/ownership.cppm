/*
 * Vector-backed ownership behind public C problem and solution views.
 *
 * Heap-owned storage objects keep C pointers stable until the matching API
 * deleter is called. Caller-owned problem views are never owned here.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <memory>
#include <vector>

module pbqp:ownership;

namespace pbqp {
struct ProblemStorage : pbqp_problem {
  std::vector<pbqp_node> node_records;
  std::vector<pbqp_edge> edge_records;
  std::vector<std::vector<pbqp_cost_t>> unary_costs;
  std::vector<std::vector<pbqp_cost_t>> pairwise_costs;

  ProblemStorage(size_t n, size_t m) : ProblemStorage(n, m, m) {}

  ProblemStorage(size_t n, size_t m, size_t matrices)
      : pbqp_problem{}, node_records(n), edge_records(m), unary_costs(n),
        pairwise_costs(matrices) {
    num_nodes = n;
    num_edges = m;
    nodes = n ? node_records.data() : nullptr;
    edges = m ? edge_records.data() : nullptr;
  }

  ProblemStorage(const ProblemStorage &) = delete;
  ProblemStorage &operator=(const ProblemStorage &) = delete;
};

class SolutionStorage : public pbqp_solution {
  std::vector<size_t> assignment;

public:
  explicit SolutionStorage(size_t n) : pbqp_solution{}, assignment(n) {
    num_nodes = n;
    options = n ? assignment.data() : nullptr;
  }

  SolutionStorage(const SolutionStorage &) = delete;
  SolutionStorage &operator=(const SolutionStorage &) = delete;
};

using SolutionPtr = std::unique_ptr<SolutionStorage>;

SolutionPtr make_solution(size_t num_nodes) {
  return std::make_unique<SolutionStorage>(num_nodes);
}
} // namespace pbqp
