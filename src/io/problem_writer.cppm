/*
 * Canonical PBQP 2 serialization with shared dense/sparse matrix definitions.
 * Matrices are interned by dimensions and exact finite/INF values, preserving
 * signed zero and original edge order. All views borrow the validated input.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <ostream>
#include <span>
#include <unordered_map>
#include <vector>
module pbqp:problem_writer;
import :text_writer;

namespace pbqp {
struct TextCost {
  pbqp_cost_t value;

  bool operator==(const TextCost &other) const noexcept {
    return value == other.value &&
           (value != 0 || std::signbit(value) == std::signbit(other.value));
  }
};

struct TextCostHash {
  size_t operator()(TextCost cost) const noexcept {
    return std::hash<pbqp_cost_t>{}(cost.value);
  }
};

struct MatrixView {
  size_t rows, cols;
  const pbqp_cost_t *costs;

  std::span<const pbqp_cost_t> values() const noexcept {
    return {costs, rows * cols};
  }

  bool operator==(const MatrixView &other) const noexcept {
    return rows == other.rows && cols == other.cols &&
           std::equal(
               values().begin(), values().end(), other.values().begin(),
               [](auto a, auto b) { return TextCost{a} == TextCost{b}; });
  }
};

struct MatrixHash {
  size_t operator()(MatrixView matrix) const noexcept {
    // Unsigned hash mixing has no relation to PBQP cost arithmetic.
    constexpr size_t mix = 0x9e3779b9;
    size_t hash = matrix.rows;
    auto combine = [&](size_t value) {
      hash ^= value + mix + (hash << 6) + (hash >> 2);
    };
    combine(matrix.cols);
    for (auto cost : matrix.values())
      combine(TextCostHash{}({cost}));
    return hash;
  }
};

class MatrixEncoding {
  MatrixView matrix;
  pbqp_cost_t default_cost;
  size_t exceptions;

public:
  explicit MatrixEncoding(MatrixView view);
  void write(std::ostream &output, size_t id) const;
};

MatrixEncoding::MatrixEncoding(MatrixView view)
    : matrix(view), default_cost(view.costs[0]), exceptions(0) {
  // Beating dense token count requires more than two thirds of all cells
  // to share a value. Such a value is a strict majority: Boyer-Moore finds it
  // with constant scratch, and the second pass verifies the actual exceptions.
  size_t votes = 0;
  for (auto cost : matrix.values()) {
    if (!votes)
      default_cost = cost;
    if (TextCost{cost} == TextCost{default_cost})
      ++votes;
    else
      --votes;
  }
  exceptions = static_cast<size_t>(std::count_if(
      matrix.values().begin(), matrix.values().end(),
      [&](auto cost) { return !(TextCost{cost} == TextCost{default_cost}); }));
}

void MatrixEncoding::write(std::ostream &output, size_t id) const {
  output << "\nMATRIX " << id << ' ' << matrix.rows << ' ' << matrix.cols;
  const size_t count = matrix.values().size();
  // Sparse uses a default and count plus three tokens per exception.
  // Compare token counts without overflowing 3 * exceptions + 2.
  if (count > 2 && exceptions <= (count - 3) / 3) {
    output << " SPARSE ";
    write_cost(output, default_cost);
    output << ' ' << exceptions << '\n';
    for (size_t index = 0; index < count; ++index)
      if (!(TextCost{matrix.costs[index]} == TextCost{default_cost})) {
        output << index / matrix.cols << ' ' << index % matrix.cols << ' ';
        write_cost(output, matrix.costs[index]);
        output << '\n';
      }
  } else {
    output << " DENSE\n";
    for (size_t index = 0; index < count; ++index) {
      write_cost(output, matrix.costs[index]);
      output << ((index + 1) % matrix.cols == 0 ? '\n' : ' ');
    }
  }
}

class ProblemWriter {
  const pbqp_problem &problem;
  std::vector<MatrixEncoding> matrices;
  std::vector<size_t> references;

public:
  explicit ProblemWriter(const pbqp_problem &p);
  void write(std::ostream &output) const;
};

ProblemWriter::ProblemWriter(const pbqp_problem &p) : problem(p) {
  std::unordered_map<MatrixView, size_t, MatrixHash> ids;
  references.reserve(p.num_edges);
  for (size_t id = 0; id < p.num_edges; ++id) {
    const auto &edge = p.edges[id];
    MatrixView matrix{p.nodes[edge.src].num_options,
                      p.nodes[edge.tgt].num_options, edge.costs};
    auto [entry, added] = ids.emplace(matrix, matrices.size());
    if (added)
      matrices.emplace_back(matrix);
    references.push_back(entry->second);
  }
}

void ProblemWriter::write(std::ostream &output) const {
  output.imbue(std::locale::classic());
  output << std::setprecision(std::numeric_limits<pbqp_cost_t>::max_digits10);
  output << "PBQP 2\nNODES " << problem.num_nodes << "\nEDGES "
         << problem.num_edges << "\nMATRICES " << matrices.size() << '\n';
  for (size_t id = 0; id < problem.num_nodes; ++id) {
    const auto &node = problem.nodes[id];
    output << "\nNODE " << id << ' ' << node.num_options << '\n';
    for (size_t option = 0; option < node.num_options; ++option) {
      write_cost(output, node.costs[option]);
      output << (option + 1 == node.num_options ? '\n' : ' ');
    }
  }
  for (size_t id = 0; id < matrices.size(); ++id)
    matrices[id].write(output, id);
  output << '\n';
  for (size_t id = 0; id < problem.num_edges; ++id) {
    const auto &edge = problem.edges[id];
    output << "EDGE " << edge.src << ' ' << edge.tgt << ' ' << references[id]
           << '\n';
  }
  output << "\nEND\n";
}
} // namespace pbqp
