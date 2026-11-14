/*
 * Shared read-only minima and prefix-conditioned bounds for exact PBQP search.
 *
 * Workers eliminate options forbidden by fixed neighbors before taking term
 * minima. Every term is still summed in reference order; no float sums are
 * regrouped. Explicit prohibitions remain separate from accumulated infinity.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <cassert>
#include <limits>
#include <numeric>
#include <vector>

module pbqp:bnb_term_bounds;
import :failure;
import :bnb_options;

namespace pbqp {
enum class InterferenceFailure {
  None,
  Disabled,
  Empty,
  NoRegisters,
  Domains,
  RegisterCost,
  MatrixCost
};

struct InterferenceCheck {
  InterferenceFailure failure = InterferenceFailure::None;
  size_t node = 0, option = 0, edge = 0, row = 0, col = 0;
  pbqp_cost_t actual = 0, expected = 0;
};

class BranchAndBoundTermBounds {
  struct EdgeMinimum {
    size_t row_offset;
    pbqp_cost_t minimum;
  };

  const pbqp_problem &problem;
  InterferenceCheck interference;
  BnbOptions optimizations;
  std::vector<size_t> offsets;
  std::vector<size_t> option_order;
  std::vector<unsigned char> initial_allowed;
  std::vector<unsigned char> initial_restricted;
  std::vector<pbqp_cost_t> unary_minima;
  std::vector<EdgeMinimum> edge_minima;
  std::vector<pbqp_cost_t> row_minima;

public:
  struct Workspace {
    std::vector<unsigned char> allowed;
    std::vector<unsigned char> restricted;

    explicit Workspace(const BranchAndBoundTermBounds &bounds)
        : allowed(bounds.num_options()), restricted(bounds.problem.num_nodes) {}
  };

private:
  void prepare_domains();
  void prepare_node(size_t id);
  void prepare_edge_minima();
  void cache_edge_minima(size_t id) noexcept;
  InterferenceCheck check_interference_structure() const noexcept;
  bool is_pure_interference_matrix(const pbqp_edge &edge, size_t domain,
                                   InterferenceCheck &result) const noexcept;
  bool canonical_prefix(const size_t *tuple, size_t depth) const noexcept;
  bool prepare_allowed(const size_t *tuple, size_t depth,
                       Workspace &workspace) const noexcept;
  pbqp_cost_t unary_bound(size_t id, const size_t *tuple, size_t depth,
                          const Workspace &workspace) const noexcept;
  pbqp_cost_t edge_bound(size_t id, const size_t *tuple, size_t depth,
                         const Workspace &workspace) const noexcept;
  pbqp_cost_t
  restricted_edge_minimum(size_t id, const size_t *tuple, size_t depth,
                          const Workspace &workspace) const noexcept;

public:
  explicit BranchAndBoundTermBounds(const pbqp_problem &p,
                                    BnbOptions options = BnbOptions{});

  static BnbOptions default_options() noexcept { return BnbOptions{}; }

  const pbqp_problem &input() const noexcept { return problem; }

  const pbqp_problem &domains() const noexcept { return problem; }

  size_t node(size_t step) const noexcept { return step; }

  bool is_pure_interference_problem() const noexcept {
    return interference.failure == InterferenceFailure::None;
  }

  const InterferenceCheck &interference_check() const noexcept {
    return interference;
  }

  void prepare_search() noexcept {}

  size_t num_options() const noexcept { return offsets.back(); }

  size_t option(size_t node, size_t rank) const noexcept {
    return option_order[offsets[node] + rank];
  }

  bool lower_bound(const size_t *tuple, size_t depth, Workspace &workspace,
                   pbqp_cost_t &cost) const noexcept;
};

BranchAndBoundTermBounds::BranchAndBoundTermBounds(const pbqp_problem &p,
                                                   BnbOptions options)
    : problem(p), optimizations(options), offsets(p.num_nodes + 1),
      initial_restricted(p.num_nodes), unary_minima(p.num_nodes) {
  prepare_domains();
  prepare_edge_minima();
  if (optimizations.structural() ||
      optimizations.enabled(BnbOptimization::RegisterSymmetry))
    interference = check_interference_structure();
  else
    interference.failure = InterferenceFailure::Disabled;
}

void BranchAndBoundTermBounds::prepare_domains() {
  const auto limit = std::numeric_limits<size_t>::max();
  for (size_t id = 0; id < problem.num_nodes; ++id) {
    if (problem.nodes[id].num_options > limit - offsets[id])
      fail(PBQP_OVERFLOW, "option workspace size overflow");
    offsets[id + 1] = offsets[id] + problem.nodes[id].num_options;
  }
  option_order.resize(num_options());
  initial_allowed.resize(num_options());
  for (size_t id = 0; id < problem.num_nodes; ++id)
    prepare_node(id);
}

void BranchAndBoundTermBounds::prepare_node(size_t id) {
  const auto &node = problem.nodes[id];
  assert(node.num_options > 0);
  auto begin = option_order.begin() + offsets[id];
  auto end = option_order.begin() + offsets[id + 1];
  std::iota(begin, end, size_t{0});
  if (optimizations.enabled(BnbOptimization::OptionOrder))
    std::sort(begin, end, [&](size_t a, size_t b) noexcept {
      if (node.costs[a] != node.costs[b])
        return node.costs[a] < node.costs[b];
      return a < b;
    });
  unary_minima[id] =
      *std::min_element(node.costs, node.costs + node.num_options);
  for (size_t option = 0; option < node.num_options; ++option) {
    bool allowed = node.costs[option] != PBQP_INF;
    initial_allowed[offsets[id] + option] = allowed;
    initial_restricted[id] |= !allowed;
  }
}

void BranchAndBoundTermBounds::prepare_edge_minima() {
  const auto limit = std::numeric_limits<size_t>::max();
  size_t rows = 0;
  edge_minima.reserve(problem.num_edges);
  for (size_t id = 0; id < problem.num_edges; ++id) {
    size_t count = problem.nodes[problem.edges[id].src].num_options;
    if (count > limit - rows)
      fail(PBQP_OVERFLOW, "row minimum count overflow");
    edge_minima.push_back({rows, PBQP_INF});
    rows += count;
  }
  row_minima.resize(rows);
  for (size_t id = 0; id < problem.num_edges; ++id)
    cache_edge_minima(id);
}

void BranchAndBoundTermBounds::cache_edge_minima(size_t id) noexcept {
  const auto &edge = problem.edges[id];
  size_t cols = problem.nodes[edge.tgt].num_options;
  assert(cols > 0 && "B&B requires validated, nonempty option domains");
  auto &minimum = edge_minima[id];
  for (size_t row = 0; row < problem.nodes[edge.src].num_options; ++row) {
    const auto *begin = edge.costs + row * cols;
    auto value = *std::min_element(begin, begin + cols);
    row_minima[minimum.row_offset + row] = value;
    minimum.minimum = std::min(minimum.minimum, value);
  }
}

bool BranchAndBoundTermBounds::is_pure_interference_matrix(
    const pbqp_edge &edge, size_t domain,
    InterferenceCheck &result) const noexcept {
  for (size_t row = 0; row < domain; ++row)
    for (size_t col = 0; col < domain; ++col) {
      auto expected = row && row == col ? PBQP_INF : 0;
      if (edge.costs[row * domain + col] != expected) {
        result.failure = InterferenceFailure::MatrixCost;
        result.row = row;
        result.col = col;
        result.actual = edge.costs[row * domain + col];
        result.expected = expected;
        return false;
      }
    }
  return true;
}

InterferenceCheck
BranchAndBoundTermBounds::check_interference_structure() const noexcept {
  // This recognizes the pure model, not every register-label symmetry.
  InterferenceCheck result;
  if (!problem.num_nodes) {
    result.failure = InterferenceFailure::Empty;
    return result;
  }
  if (problem.nodes[0].num_options < 2) {
    result.failure = InterferenceFailure::NoRegisters;
    return result;
  }
  size_t domain = problem.nodes[0].num_options;
  for (size_t id = 0; id < problem.num_nodes; ++id) {
    result.node = id;
    const auto &node = problem.nodes[id];
    if (node.num_options != domain) {
      result.failure = InterferenceFailure::Domains;
      return result;
    }
    for (size_t option = 1; option < domain; ++option)
      if (node.costs[option] != 0) {
        result.failure = InterferenceFailure::RegisterCost;
        result.option = option;
        result.actual = node.costs[option];
        return result;
      }
  }
  for (size_t id = 0; id < problem.num_edges; ++id) {
    result.edge = id;
    if (!is_pure_interference_matrix(problem.edges[id], domain, result))
      return result;
  }
  return result;
}

bool BranchAndBoundTermBounds::canonical_prefix(const size_t *tuple,
                                                size_t depth) const noexcept {
  if (!optimizations.enabled(BnbOptimization::RegisterSymmetry) ||
      !is_pure_interference_problem())
    return true;
  size_t used = 0;
  for (size_t id = 0; id < depth; ++id) {
    if (tuple[id] > used + 1)
      return false;
    used = std::max(used, tuple[id]);
  }
  return true;
}

bool BranchAndBoundTermBounds::prepare_allowed(
    const size_t *tuple, size_t depth, Workspace &workspace) const noexcept {
  std::copy(initial_allowed.begin(), initial_allowed.end(),
            workspace.allowed.begin());
  std::copy(initial_restricted.begin(), initial_restricted.end(),
            workspace.restricted.begin());
  // Forward checking uses only explicit constraints, never rounded costs.
  for (size_t id = 0; id < problem.num_edges; ++id) {
    const auto &edge = problem.edges[id];
    if (edge.src >= depth)
      continue;
    size_t cols = problem.nodes[edge.tgt].num_options;
    const auto *row = edge.costs + tuple[edge.src] * cols;
    if (edge.tgt < depth) {
      if (row[tuple[edge.tgt]] == PBQP_INF)
        return false;
    } else if (optimizations.enabled(BnbOptimization::ForwardChecking)) {
      for (size_t col = 0; col < cols; ++col)
        if (row[col] == PBQP_INF) {
          workspace.allowed[offsets[edge.tgt] + col] = 0;
          workspace.restricted[edge.tgt] = 1;
        }
    }
  }
  return true;
}

pbqp_cost_t BranchAndBoundTermBounds::unary_bound(
    size_t id, const size_t *tuple, size_t depth,
    const Workspace &workspace) const noexcept {
  if (id < depth)
    return problem.nodes[id].costs[tuple[id]];
  if (!workspace.restricted[id])
    return unary_minima[id];
  if (optimizations.enabled(BnbOptimization::OptionOrder)) {
    for (size_t rank = 0; rank < problem.nodes[id].num_options; ++rank) {
      size_t selected = option(id, rank);
      if (workspace.allowed[offsets[id] + selected])
        return problem.nodes[id].costs[selected];
    }
    return PBQP_INF;
  }
  auto minimum = PBQP_INF;
  for (size_t selected = 0; selected < problem.nodes[id].num_options;
       ++selected)
    if (workspace.allowed[offsets[id] + selected])
      minimum = std::min(minimum, problem.nodes[id].costs[selected]);
  return minimum;
}

pbqp_cost_t BranchAndBoundTermBounds::restricted_edge_minimum(
    size_t id, const size_t *tuple, size_t depth,
    const Workspace &workspace) const noexcept {
  const auto &edge = problem.edges[id];
  size_t cols = problem.nodes[edge.tgt].num_options;
  auto floor = edge.src < depth
                   ? row_minima[edge_minima[id].row_offset + tuple[edge.src]]
                   : edge_minima[id].minimum;
  size_t begin = edge.src < depth ? tuple[edge.src] : 0;
  size_t end =
      edge.src < depth ? begin + 1 : problem.nodes[edge.src].num_options;
  auto minimum = PBQP_INF;
  for (size_t row = begin; row < end; ++row) {
    if (!workspace.allowed[offsets[edge.src] + row])
      continue;
    for (size_t col = 0; col < cols; ++col)
      if (workspace.allowed[offsets[edge.tgt] + col]) {
        minimum = std::min(minimum, edge.costs[row * cols + col]);
        if (minimum == floor)
          return minimum;
      }
  }
  return minimum;
}

pbqp_cost_t BranchAndBoundTermBounds::edge_bound(
    size_t id, const size_t *tuple, size_t depth,
    const Workspace &workspace) const noexcept {
  const auto &edge = problem.edges[id];
  if (edge.tgt < depth)
    return edge.costs[tuple[edge.src] * problem.nodes[edge.tgt].num_options +
                      tuple[edge.tgt]];
  if (edge.src < depth && !workspace.restricted[edge.tgt])
    return row_minima[edge_minima[id].row_offset + tuple[edge.src]];
  if (edge.src >= depth && !workspace.restricted[edge.src] &&
      !workspace.restricted[edge.tgt])
    return edge_minima[id].minimum;
  return restricted_edge_minimum(id, tuple, depth, workspace);
}

bool BranchAndBoundTermBounds::lower_bound(const size_t *tuple, size_t depth,
                                           Workspace &workspace,
                                           pbqp_cost_t &cost) const noexcept {
  if (!canonical_prefix(tuple, depth) ||
      !prepare_allowed(tuple, depth, workspace))
    return false;
  // Each term independently bounds the corresponding reference term. Keeping
  // reference order preserves this bound through rounded addition and overflow.
  cost = 0;
  for (size_t id = 0; id < problem.num_nodes; ++id) {
    auto term = unary_bound(id, tuple, depth, workspace);
    if (term == PBQP_INF)
      return false;
    cost = pbqp_cost_add(cost, term);
  }
  for (size_t id = 0; id < problem.num_edges; ++id) {
    auto term = edge_bound(id, tuple, depth, workspace);
    if (term == PBQP_INF)
      return false;
    cost = pbqp_cost_add(cost, term);
  }
  return true;
}
} // namespace pbqp
