/*
 * PBQP 1/2 problem parsing over the shared stream tokenizer.
 * Sparse definitions stay compact until first use; edges share dense arrays.
 * Checks dimensions, sparse coordinates, and references before publishing a
 * view.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
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
  struct Entry {
    size_t index, line;
    pbqp_cost_t cost;
  };

  struct Matrix {
    size_t rows = 0, cols = 0;
    pbqp_cost_t default_cost = 0;
    std::vector<pbqp_cost_t> dense;
    std::vector<Entry> overrides;
  };

  TextReader reader;
  std::unique_ptr<ProblemStorage> problem;
  std::vector<Matrix> definitions;
  unsigned version = 1;

  void header();
  void nodes();
  void matrices();
  void sparse(Matrix &matrix, size_t cells);
  const pbqp_cost_t *materialize(size_t id);
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
  check_count<pbqp_node>(n, reader.token_line());
  check_count<std::vector<pbqp_cost_t>>(n, reader.token_line());
  reader.expect("EDGES");
  const size_t m = reader.size();
  check_count<pbqp_edge>(m, reader.token_line());
  size_t count = m;
  if (version == 2) {
    reader.expect("MATRICES");
    count = reader.size();
    check_count<Matrix>(count, reader.token_line());
    check_count<std::vector<pbqp_cost_t>>(count, reader.token_line());
    definitions.resize(count);
  } else {
    check_count<std::vector<pbqp_cost_t>>(count, reader.token_line());
  }
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

void ProblemReader::sparse(Matrix &matrix, size_t cells) {
  matrix.default_cost = reader.cost();
  const auto count = reader.size();
  if (count > cells)
    fail(PBQP_INVALID, "too many sparse matrix entries", reader.token_line());
  // Grow only as actual input arrives: an unused huge default-filled matrix
  // requires no cell-sized allocation, bitmap, or per-coordinate tree nodes.
  for (size_t id = 0; id < count; ++id) {
    const size_t row = reader.size(), col = reader.size();
    if (row >= matrix.rows || col >= matrix.cols)
      fail(PBQP_INVALID, "sparse matrix coordinate out of range",
           reader.token_line());
    const size_t line = reader.token_line();
    const auto cost = reader.cost();
    matrix.overrides.push_back({row * matrix.cols + col, line, cost});
  }
  std::sort(matrix.overrides.begin(), matrix.overrides.end(),
            [](const Entry &a, const Entry &b) { return a.index < b.index; });
  const auto duplicate = std::adjacent_find(
      matrix.overrides.begin(), matrix.overrides.end(),
      [](const Entry &a, const Entry &b) { return a.index == b.index; });
  if (duplicate != matrix.overrides.end())
    fail(PBQP_INVALID, "duplicate sparse matrix coordinate", duplicate->line);
}

void ProblemReader::matrices() {
  for (size_t id = 0; id < definitions.size(); ++id) {
    reader.expect("MATRIX");
    if (reader.size() != id)
      fail(PBQP_INVALID,
           "matrices must be numbered in increasing order from zero",
           reader.token_line());
    auto &matrix = definitions[id];
    matrix.rows = reader.size();
    matrix.cols = reader.size();
    const auto count =
        matrix_size(matrix.rows, matrix.cols, reader.token_line());
    auto mode = reader.word();
    if (mode == "DENSE") {
      // No large upfront allocation for a truncated definition either.
      for (size_t cell = 0; cell < count; ++cell)
        matrix.dense.push_back(reader.cost());
    } else if (mode == "SPARSE") {
      sparse(matrix, count);
    } else {
      fail(PBQP_INVALID, "expected DENSE or SPARSE matrix encoding",
           reader.token_line());
    }
  }
}

const pbqp_cost_t *ProblemReader::materialize(size_t id) {
  auto &costs = problem->pairwise_costs[id];
  if (!costs.empty())
    return costs.data();
  auto &matrix = definitions[id];
  if (!matrix.dense.empty()) {
    costs = std::move(matrix.dense);
  } else {
    costs.assign(matrix.rows * matrix.cols, matrix.default_cost);
    for (const auto &entry : matrix.overrides)
      costs[entry.index] = entry.cost;
    matrix.overrides = std::vector<Entry>{};
  }
  return costs.data();
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
      if (matrix >= definitions.size())
        fail(PBQP_INVALID, "undefined matrix reference", reader.token_line());
      const auto &shape = definitions[matrix];
      if (shape.rows != problem->nodes[edge.src].num_options ||
          shape.cols != problem->nodes[edge.tgt].num_options)
        fail(PBQP_INVALID, "matrix dimensions do not match edge domains",
             reader.token_line());
      edge.costs = materialize(matrix);
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
