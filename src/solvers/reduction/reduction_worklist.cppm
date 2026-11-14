/*
 * Incremental low-degree and RN candidate queues for PBQP reduction.
 *
 * Caches each active edge's directed availability contributions. Updates are
 * local to changed costs, matrices, and adjacency, including neighbor-domain
 * feasibility changes. Queue keys are immutable snapshots between refreshes.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <set>
#include <vector>

module pbqp:reduction_worklist;
import :reduction_graph;

namespace pbqp {
class ReductionWorklist {
  enum class Group { None, LowDegree, Safe, Spill };

  struct Priority {
    ReductionCost cost;
    size_t degree = 0;
    size_t id = 0;
  };

  struct LessPriority {
    bool operator()(const Priority &a, const Priority &b) const noexcept {
      if (better_reduction_cost(a.cost, b.cost))
        return true;
      if (better_reduction_cost(b.cost, a.cost))
        return false;
      if (a.degree != b.degree)
        return a.degree < b.degree;
      return a.id < b.id;
    }
  };

  struct NodeState {
    size_t available = 0;
    size_t denied = 0;
    std::vector<size_t> unsafe_edges;
    Group group = Group::None;
    Priority priority;
    bool dirty = false;
  };

  struct Direction {
    size_t worst = 0;
    std::vector<unsigned char> unsafe;
  };

  struct EdgeState {
    Direction src;
    Direction tgt;
    bool active = false;
  };

  const ReductionGraph &graph;
  std::vector<NodeState> nodes;
  std::vector<EdgeState> edges;
  std::vector<size_t> dirty;
  std::set<size_t> low_degree;
  std::set<size_t> safe;
  std::set<Priority, LessPriority> spill;

  void mark(size_t id);
  void count_options(size_t id) noexcept;
  void compute_direction(size_t edge, size_t id, Direction &result);
  void add_direction(size_t id, const Direction &value);
  void remove_direction(size_t id, const Direction &value);
  bool is_safe(size_t id) const noexcept;
  void refresh_node(size_t id);

public:
  explicit ReductionWorklist(const ReductionGraph &g);
  size_t next_node() const noexcept;
  void costs_changed(size_t id, bool feasibility_changed);
  void refresh_edge(size_t edge);
  void remove_edge(size_t edge);
  void flush();
};

ReductionWorklist::ReductionWorklist(const ReductionGraph &g)
    : graph(g), nodes(g.num_nodes()), edges(g.num_edges()) {
  dirty.reserve(graph.num_nodes());
  for (size_t id = 0; id < graph.num_nodes(); ++id) {
    nodes[id].unsafe_edges.resize(graph.node(id).costs.size());
    count_options(id);
    mark(id);
  }
  for (size_t edge = 0; edge < graph.num_edges(); ++edge)
    refresh_edge(edge);
  flush();
}

void ReductionWorklist::mark(size_t id) {
  if (!nodes[id].dirty) {
    nodes[id].dirty = true;
    dirty.push_back(id);
  }
}

void ReductionWorklist::count_options(size_t id) noexcept {
  auto &state = nodes[id];
  state.available = 0;
  const auto &costs = graph.node(id).costs;
  for (size_t option = 1; option < costs.size(); ++option)
    state.available += costs[option].feasible;
}

void ReductionWorklist::compute_direction(size_t edge, size_t id,
                                          Direction &result) {
  const auto &costs = graph.node(id).costs;
  const auto &other_costs = graph.node(graph.other(edge, id)).costs;
  result.unsafe.assign(costs.size(), 0);
  result.worst = 0;
  for (size_t other = 0; other < other_costs.size(); ++other) {
    if (!other_costs[other].feasible)
      continue;
    size_t blocked = 0;
    for (size_t option = 1; option < costs.size(); ++option)
      if (costs[option].feasible &&
          !graph.cost(edge, id, option, other).feasible) {
        ++blocked;
        result.unsafe[option] = 1;
      }
    result.worst = std::max(result.worst, blocked);
  }
}

void ReductionWorklist::add_direction(size_t id, const Direction &value) {
  auto &state = nodes[id];
  state.denied += value.worst;
  for (size_t option = 1; option < value.unsafe.size(); ++option)
    state.unsafe_edges[option] += value.unsafe[option];
  mark(id);
}

void ReductionWorklist::remove_direction(size_t id, const Direction &value) {
  auto &state = nodes[id];
  state.denied -= value.worst;
  for (size_t option = 1; option < value.unsafe.size(); ++option)
    state.unsafe_edges[option] -= value.unsafe[option];
  mark(id);
}

void ReductionWorklist::refresh_edge(size_t edge) {
  if (edges.size() <= edge)
    edges.resize(edge + 1);
  auto &state = edges[edge];
  size_t src = graph.source(edge), tgt = graph.target(edge);
  if (state.active) {
    remove_direction(src, state.src);
    remove_direction(tgt, state.tgt);
  }
  compute_direction(edge, src, state.src);
  compute_direction(edge, tgt, state.tgt);
  add_direction(src, state.src);
  add_direction(tgt, state.tgt);
  state.active = true;
}

void ReductionWorklist::remove_edge(size_t edge) {
  auto &state = edges[edge];
  remove_direction(graph.source(edge), state.src);
  remove_direction(graph.target(edge), state.tgt);
  state.active = false;
}

void ReductionWorklist::costs_changed(size_t id, bool feasibility_changed) {
  // Feasibility changes alter both directions of every incident edge. Merely
  // changing a finite score affects this node's RN priority, not availability.
  if (feasibility_changed) {
    count_options(id);
    for (size_t edge : graph.node(id).edges)
      refresh_edge(edge);
  }
  mark(id);
}

bool ReductionWorklist::is_safe(size_t id) const noexcept {
  const auto &state = nodes[id];
  if (state.denied < state.available)
    return true;
  const auto &costs = graph.node(id).costs;
  for (size_t option = 1; option < costs.size(); ++option)
    if (costs[option].feasible && state.unsafe_edges[option] == 0)
      return true;
  return false;
}

void ReductionWorklist::refresh_node(size_t id) {
  auto &state = nodes[id];
  switch (state.group) {
  case Group::LowDegree:
    low_degree.erase(id);
    break;
  case Group::Safe:
    safe.erase(id);
    break;
  case Group::Spill:
    spill.erase(state.priority);
    break;
  case Group::None:
    break;
  }
  state.group = Group::None;
  if (!graph.node(id).active)
    return;
  if (graph.degree(id) <= 2) {
    state.group = Group::LowDegree;
    low_degree.insert(id);
  } else if (is_safe(id)) {
    state.group = Group::Safe;
    safe.insert(id);
  } else {
    state.group = Group::Spill;
    state.priority = {graph.node(id).costs[0], graph.degree(id), id};
    spill.insert(state.priority);
  }
}

void ReductionWorklist::flush() {
  for (size_t id : dirty) {
    refresh_node(id);
    nodes[id].dirty = false;
  }
  dirty.clear();
}

size_t ReductionWorklist::next_node() const noexcept {
  if (!low_degree.empty())
    return *low_degree.begin();
  if (!safe.empty())
    return *safe.begin();
  return spill.begin()->id;
}
} // namespace pbqp
