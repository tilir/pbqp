/*
 * PBQP 1/2 problem parsing over the shared stream tokenizer.
 * Named matrices own one dense cost array each; edges borrow those arrays.
 * Checks dimensions, sparse coordinates, and references before publishing a
 * view.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <istream>
#include <memory>
#include <set>
#include <string_view>
#include <utility>
#include <vector>
module pbqp:problem_reader;
import :text_reader;
import :problem;
import :ownership;
import :failure;

namespace pbqp {
class ProblemReader {
  struct Dimensions {
    size_t rows, cols;
  };

  TextReader reader;
  std::unique_ptr<ProblemStorage> problem;
  std::vector<Dimensions> dimensions;
  unsigned version = 1;

  void header();
  void nodes();
  void matrices();
  void sparse(std::vector<pbqp_cost_t> &costs, Dimensions shape, size_t cells);
  void edges();

public:
  explicit ProblemReader(std::istream &input) : reader(input) {}

  std::unique_ptr<ProblemStorage> read();
};

void ProblemReader::header() {
  reader.expect("PBQP");
  auto token = reader.word();
  if (token == "1")
    version = 1;
  else if (token == "2")
    version = 2;
  else
    fail(PBQP_INVALID, "unsupported PBQP format version", reader.token_line());
  reader.expect("NODES");
  const size_t n = reader.size();
  reader.expect("EDGES");
  const size_t m = reader.size();
  size_t count = m;
  if (version == 2) {
    reader.expect("MATRICES");
    count = reader.size();
    check_count<Dimensions>(count, reader.token_line());
    dimensions.resize(count);
  }
  check_count<pbqp_node>(n, reader.token_line());
  check_count<pbqp_edge>(m, reader.token_line());
  check_count<std::vector<pbqp_cost_t>>(count, reader.token_line());
  problem = std::make_unique<ProblemStorage>(n, m, count);
}

void ProblemReader::nodes() {
  for (size_t id = 0; id < problem->num_nodes; ++id) {
    reader.expect("NODE");
    if (reader.size() != id)
      fail(PBQP_INVALID, "nodes must be numbered in increasing order from zero",
           reader.token_line());
    auto &node = problem->node_records[id];
    node.num_options = reader.size();
    if (!node.num_options)
      fail(PBQP_INVALID, "node must have at least one option",
           reader.token_line());
    check_count<pbqp_cost_t>(node.num_options, reader.token_line());
    auto &costs = problem->unary_costs[id];
    costs.resize(node.num_options);
    for (auto &cost : costs)
      cost = reader.cost();
    node.costs = costs.data();
  }
}

void ProblemReader::sparse(std::vector<pbqp_cost_t> &costs, Dimensions shape,
                           size_t cells) {
  const auto value = reader.cost();
  const auto count = reader.size();
  if (count > cells)
    fail(PBQP_INVALID, "too many sparse matrix entries", reader.token_line());
  costs.assign(cells, value);
  std::set<size_t> occupied;
  for (size_t id = 0; id < count; ++id) {
    const size_t row = reader.size(), col = reader.size();
    if (row >= shape.rows || col >= shape.cols)
      fail(PBQP_INVALID, "sparse matrix coordinate out of range",
           reader.token_line());
    const size_t index = row * shape.cols + col;
    if (!occupied.insert(index).second)
      fail(PBQP_INVALID, "duplicate sparse matrix coordinate",
           reader.token_line());
    costs[index] = reader.cost();
  }
}

void ProblemReader::matrices() {
  for (size_t id = 0; id < dimensions.size(); ++id) {
    reader.expect("MATRIX");
    if (reader.size() != id)
      fail(PBQP_INVALID,
           "matrices must be numbered in increasing order from zero",
           reader.token_line());
    Dimensions shape{reader.size(), reader.size()};
    const auto count = matrix_size(shape.rows, shape.cols, reader.token_line());
    dimensions[id] = shape;
    auto &costs = problem->pairwise_costs[id];
    auto mode = reader.word();
    if (mode == "DENSE") {
      costs.resize(count);
      for (auto &cost : costs)
        cost = reader.cost();
    } else if (mode == "SPARSE") {
      sparse(costs, shape, count);
    } else {
      fail(PBQP_INVALID, "expected DENSE or SPARSE matrix encoding",
           reader.token_line());
    }
  }
}

void ProblemReader::edges() {
  std::set<std::pair<size_t, size_t>> endpoints;
  for (size_t id = 0; id < problem->num_edges; ++id) {
    reader.expect("EDGE");
    auto &edge = problem->edge_records[id];
    edge.src = reader.size();
    edge.tgt = reader.size();
    if (edge.src >= edge.tgt || edge.tgt >= problem->num_nodes)
      fail(PBQP_INVALID, "edge must satisfy 0 <= src < tgt < NODES",
           reader.token_line());
    if (!endpoints.emplace(edge.src, edge.tgt).second)
      fail(PBQP_INVALID, "duplicate edge", reader.token_line());
    if (version == 1) {
      auto &costs = problem->pairwise_costs[id];
      costs.resize(matrix_size(*problem, edge, reader.token_line()));
      for (auto &cost : costs)
        cost = reader.cost();
      edge.costs = costs.data();
    } else {
      const size_t matrix = reader.size();
      if (matrix >= dimensions.size())
        fail(PBQP_INVALID, "undefined matrix reference", reader.token_line());
      const auto shape = dimensions[matrix];
      if (shape.rows != problem->nodes[edge.src].num_options ||
          shape.cols != problem->nodes[edge.tgt].num_options)
        fail(PBQP_INVALID, "matrix dimensions do not match edge domains",
             reader.token_line());
      edge.costs = problem->pairwise_costs[matrix].data();
    }
  }
}

std::unique_ptr<ProblemStorage> ProblemReader::read() {
  header();
  nodes();
  if (version == 2)
    matrices();
  edges();
  reader.expect("END");
  if (reader.next())
    fail(PBQP_INVALID, "unexpected data after END", reader.token_line());
  return std::move(problem);
}
} // namespace pbqp
