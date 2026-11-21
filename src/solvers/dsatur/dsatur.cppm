/*
 * Bounded DSATUR construction, tabu recoloring, and spill repair for the pure
 * interference model. Intermediate colorings may conflict; only reference-
 * evaluated feasible assignments are retained. All workspace is prepared
 * before run(), whose local search neither allocates nor throws.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <numeric>
#include <random>
#include <span>
#include <vector>
module pbqp:dsatur;
import :interference;
import :deadline;
import :ownership;
import :evaluation;

namespace pbqp {
enum class DsaturState {
  Unsupported,
  MemoryBudget,
  Ready,
  WorkBudget,
  RestartBudget,
  Expired,
  Colored
};

struct DsaturSettings {
  static constexpr size_t memory_budget = 32 * 1024 * 1024;
  size_t work_budget = 8 * 1024 * 1024;
  size_t restarts = 8;
};

class DsaturSearch {
  static constexpr size_t moves_per_restart = 8192, stagnation_moves = 512;
  static constexpr size_t tenure_base = 7, tenure_jitter = 10;
  const pbqp_problem &problem;
  DsaturSettings settings;
  Deadline deadline;
  DsaturState state = DsaturState::Unsupported;
  size_t registers = 0, work = 0, moves = 0, restarts = 0, polls = 0;
  bool found = false;
  std::vector<size_t> offsets, adjacent, colors, rank, saturation;
  std::vector<size_t> counts, tabu, repair_counts, tuple;
  SolutionPtr best;
  std::mt19937 random{1};

  bool fits_storage() const noexcept;
  bool charge(size_t amount = 1) noexcept;
  bool construct() noexcept;

  struct Move {
    size_t node, color = 0, conflicts = std::numeric_limits<size_t>::max(),
                 bad = 0;
  };

  bool choose_recoloring(size_t node, size_t remaining_conflicts,
                         size_t best_conflicts, size_t iteration, Move &move,
                         size_t &ties) noexcept;
  bool choose_move(size_t conflicts, size_t best_conflicts, size_t iteration,
                   Move &move) noexcept;
  bool apply_move(const Move &move, size_t iteration) noexcept;
  bool improve() noexcept;

  struct SpillChoice {
    size_t node;
    bool has_conflicts = false;
  };

  void prepare_repair() noexcept;
  bool spill_negative_costs() noexcept;
  bool choose_spill(SpillChoice &choice) noexcept;
  bool repair() noexcept;
  bool keep() noexcept;
  void spill(size_t node) noexcept;

  std::span<const size_t> neighbors(size_t node) const noexcept {
    return std::span<const size_t>(adjacent).subspan(
        offsets[node], offsets[node + 1] - offsets[node]);
  }

  size_t degree(size_t node) const noexcept {
    return offsets[node + 1] - offsets[node];
  }

  size_t &count(size_t node, size_t color) noexcept {
    return counts[node * registers + color];
  }

public:
  explicit DsaturSearch(const pbqp_problem &p, DsaturSettings limits = {},
                        Deadline limit = Deadline{})
      : problem(p), settings(limits), deadline(limit) {}

  ~DsaturSearch() noexcept;

  bool prepare(const InterferenceCheck &check);
  void run() noexcept;

  bool ready() const noexcept { return state == DsaturState::Ready; }

  bool has_solution() const noexcept { return found; }

  DsaturState status() const noexcept { return state; }

  size_t work_used() const noexcept { return work; }

  size_t moves_used() const noexcept { return moves; }

  size_t restarts_used() const noexcept { return restarts; }

  const pbqp_solution &result() const noexcept {
    assert(found);
    return *best;
  }

  pbqp_solution *release_solution() noexcept { return best.release(); }
};

DsaturSearch::~DsaturSearch() noexcept = default;

bool DsaturSearch::fits_storage() const noexcept {
  // Conservative payload/metadata accounting, checked without overflowing.
  // The fixed reserve covers the random engine and vector/control objects.
  constexpr size_t reserve = 4096, per_node = 12 * sizeof(size_t);
  size_t remaining = DsaturSettings::memory_budget - reserve;
  auto take = [&](size_t count, size_t bytes) {
    if (count > remaining / bytes)
      return false;
    remaining -= count * bytes;
    return true;
  };
  if (!take(problem.num_nodes, per_node) ||
      !take(problem.num_edges, 2 * sizeof(size_t)))
    return false;
  return registers <= remaining / (2 * sizeof(size_t)) / problem.num_nodes;
}

bool DsaturSearch::prepare(const InterferenceCheck &check) {
  if (check.failure != InterferenceFailure::None)
    return false;
  registers = problem.nodes[0].num_options - 1;
  if (!fits_storage()) {
    state = DsaturState::MemoryBudget;
    return false;
  }
  const size_t nodes = problem.num_nodes;
  offsets.assign(nodes + 1, 0);
  for (size_t edge = 0; edge < problem.num_edges; ++edge) {
    ++offsets[problem.edges[edge].src + 1];
    ++offsets[problem.edges[edge].tgt + 1];
  }
  std::partial_sum(offsets.begin(), offsets.end(), offsets.begin());
  adjacent.resize(2 * problem.num_edges);
  rank.assign(offsets.begin(), offsets.end() - 1);
  for (size_t edge = 0; edge < problem.num_edges; ++edge) {
    const auto &e = problem.edges[edge];
    adjacent[rank[e.src]++] = e.tgt;
    adjacent[rank[e.tgt]++] = e.src;
  }
  std::iota(rank.begin(), rank.end(), size_t{0});
  colors.resize(nodes);
  saturation.resize(nodes);
  counts.resize(nodes * registers);
  tabu.resize(counts.size());
  repair_counts.resize(nodes);
  tuple.resize(nodes);
  best = make_solution(nodes);
  state = DsaturState::Ready;
  return true;
}

bool DsaturSearch::charge(size_t amount) noexcept {
  if (amount > settings.work_budget - work) {
    state = DsaturState::WorkBudget;
    return false;
  }
  work += amount;
  // Polling is batched; copies of the immutable deadline share its start time.
  if (polls++ % 256 == 0 && deadline.expired()) {
    state = DsaturState::Expired;
    return false;
  }
  return true;
}

bool DsaturSearch::construct() noexcept {
  std::fill(colors.begin(), colors.end(), registers);
  std::fill(saturation.begin(), saturation.end(), 0);
  std::fill(counts.begin(), counts.end(), 0);
  std::fill(tabu.begin(), tabu.end(), 0);
  // Randomness affects tie ranking only, never feasibility or a pruning bound.
  for (size_t size = rank.size(); size > 1; --size)
    std::swap(rank[size - 1], rank[random() % size]);
  for (size_t step = 0; step < colors.size(); ++step) {
    size_t chosen = colors.size();
    for (size_t node : rank) {
      if (!charge())
        return false;
      if (colors[node] != registers)
        continue;
      if (chosen == colors.size() || saturation[node] > saturation[chosen] ||
          (saturation[node] == saturation[chosen] &&
           degree(node) > degree(chosen)))
        chosen = node;
    }
    size_t color = 0;
    for (size_t c = 1; c < registers; ++c) {
      if (!charge())
        return false;
      if (count(chosen, c) < count(chosen, color))
        color = c;
    }
    colors[chosen] = color;
    for (size_t neighbor : neighbors(chosen)) {
      if (!charge())
        return false;
      if (count(neighbor, color)++ == 0 && colors[neighbor] == registers)
        ++saturation[neighbor];
    }
  }
  return true;
}

void DsaturSearch::spill(size_t node) noexcept {
  tuple[node] = 0;
  repair_counts[node] = 0;
  for (size_t neighbor : neighbors(node))
    if (tuple[neighbor] && colors[node] == colors[neighbor])
      --repair_counts[neighbor];
}

bool DsaturSearch::keep() noexcept {
  if (!charge(problem.num_nodes + problem.num_edges))
    return false;
  pbqp_cost_t cost;
  if (evaluate_assignment(problem, tuple.data(), cost) &&
      (!found || cost < best->cost)) {
    found = true;
    best->cost = cost;
    best->quality = PBQP_SOLUTION_FEASIBLE;
    std::copy(tuple.begin(), tuple.end(), best->options);
  }
  return true;
}

void DsaturSearch::prepare_repair() noexcept {
  for (size_t node = 0; node < colors.size(); ++node) {
    tuple[node] = colors[node] + 1;
    repair_counts[node] = count(node, colors[node]);
  }
}

bool DsaturSearch::spill_negative_costs() noexcept {
  // A negative spill improves its original unary term and only removes
  // conflicts.
  for (size_t node = 0; node < colors.size(); ++node)
    if (problem.nodes[node].costs[0] < 0) {
      if (!charge(degree(node) + 1))
        return false;
      spill(node);
    }
  return true;
}

bool DsaturSearch::choose_spill(SpillChoice &choice) noexcept {
  choice = {colors.size()};
  double best_ratio = std::numeric_limits<double>::infinity();
  for (size_t node = 0; node < colors.size(); ++node) {
    if (!charge())
      return false;
    if (!repair_counts[node])
      continue;
    choice.has_conflicts = true;
    const auto cost = problem.nodes[node].costs[0];
    if (cost == PBQP_INF)
      continue;
    const double ratio = static_cast<double>(cost) / repair_counts[node];
    if (choice.node == colors.size() || ratio < best_ratio) {
      choice.node = node;
      best_ratio = ratio;
    }
  }
  return true;
}

bool DsaturSearch::repair() noexcept {
  prepare_repair();
  if (!spill_negative_costs())
    return false;
  while (true) {
    SpillChoice choice{};
    if (!choose_spill(choice))
      return false;
    if (!choice.has_conflicts)
      return keep();
    // Forbidden spills can prevent repair; this is not an infeasibility proof.
    if (choice.node == colors.size())
      return true;
    if (!charge(degree(choice.node) + 1))
      return false;
    spill(choice.node);
  }
}

bool DsaturSearch::choose_recoloring(size_t node, size_t remaining_conflicts,
                                     size_t best_conflicts, size_t iteration,
                                     Move &move, size_t &ties) noexcept {
  for (size_t color = 0; color < registers; ++color) {
    if (!charge())
      return false;
    if (color == colors[node])
      continue;
    const size_t score = remaining_conflicts + count(node, color);
    // Aspiration allows a forbidden reverse move only for a new conflict
    // record. This score ranks moves; it is never a PBQP pruning bound.
    if (tabu[node * registers + color] > iteration && score >= best_conflicts)
      continue;
    if (score < move.conflicts) {
      move.node = node;
      move.color = color;
      move.conflicts = score;
      ties = 1;
    } else if (score == move.conflicts && random() % ++ties == 0) {
      move.node = node;
      move.color = color;
    }
  }
  return true;
}

bool DsaturSearch::choose_move(size_t conflicts, size_t best_conflicts,
                               size_t iteration, Move &move) noexcept {
  size_t ties = 0;
  for (size_t node = 0; node < colors.size(); ++node) {
    if (!charge())
      return false;
    const size_t old = count(node, colors[node]);
    if (!old)
      continue;
    ++move.bad;
    if (!choose_recoloring(node, conflicts - old, best_conflicts, iteration,
                           move, ties))
      return false;
  }
  return true;
}

bool DsaturSearch::apply_move(const Move &move, size_t iteration) noexcept {
  if (!charge(degree(move.node) + 1))
    return false;
  const size_t old_color = colors[move.node];
  tabu[move.node * registers + old_color] =
      iteration + tenure_base + 3 * move.bad / 5 + random() % tenure_jitter;
  for (size_t neighbor : neighbors(move.node)) {
    --count(neighbor, old_color);
    ++count(neighbor, move.color);
  }
  colors[move.node] = move.color;
  ++moves;
  return true;
}

bool DsaturSearch::improve() noexcept {
  // rank is a permutation of all nodes; each conflicting edge is counted twice.
  size_t conflicts = std::accumulate(rank.begin(), rank.end(), size_t{0},
                                     [this](size_t total, size_t node) {
                                       return total + count(node, colors[node]);
                                     });
  conflicts /= 2;
  size_t best_conflicts = conflicts, last_improvement = 0;
  if (!repair())
    return false;
  for (size_t iteration = 1; iteration <= moves_per_restart; ++iteration) {
    if (!conflicts) {
      state = DsaturState::Colored;
      return false;
    }
    if (iteration - last_improvement > stagnation_moves)
      return true;
    Move move{colors.size()};
    if (!choose_move(conflicts, best_conflicts, iteration, move))
      return false;
    if (move.node == colors.size())
      return true;
    if (!apply_move(move, iteration))
      return false;
    conflicts = move.conflicts;
    if (conflicts < best_conflicts) {
      best_conflicts = conflicts;
      last_improvement = iteration;
      if (!repair())
        return false;
    }
  }
  return true;
}

void DsaturSearch::run() noexcept {
  if (!ready())
    return;
  for (size_t attempt = 0; attempt < settings.restarts; ++attempt) {
    if (!charge())
      return;
    ++restarts;
    if (!construct() || !improve())
      return;
  }
  state = DsaturState::RestartBudget;
}
} // namespace pbqp
