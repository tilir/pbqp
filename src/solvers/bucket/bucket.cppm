/*
 * Dense min-plus bucket kernels and reverse assignment reconstruction.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <ranges>
#include <span>
#include <utility>
#include <variant>
#include <vector>
module pbqp:bucket;
import :bucket_plan;
import :deadline;
import :parallel;
import :ownership;
import :evaluation;

namespace pbqp {
// Wider ordinary addition keeps finite intermediate costs away from the INF
// sentinel. The bound also covers the rounding accumulated across all terms.
static_assert(std::numeric_limits<double>::max_exponent >
              std::numeric_limits<pbqp_cost_t>::max_exponent +
                  2 * std::numeric_limits<size_t>::digits);

struct BucketFactor {
  std::vector<size_t> scope;
  std::span<const pbqp_cost_t> input;
  std::vector<double> table;
};

struct BucketProjection {
  const pbqp_cost_t *input;
  const double *table;
  size_t stride = 0;
  std::vector<size_t> separator_strides;

  double cost(size_t index) const noexcept {
    return input ? static_cast<double>(input[index]) : table[index];
  }
};

struct BucketInput {
  size_t domain, cells;
  std::vector<size_t> separator_domains;
  std::vector<BucketProjection> factors;
};

class BucketChoices {
  using Storage =
      std::variant<std::vector<std::uint8_t>, std::vector<std::uint16_t>,
                   std::vector<std::uint32_t>, std::vector<size_t>>;
  Storage values;

public:
  template <typename Fill>
  void allocate(size_t domain, size_t cells, Fill fill) {
    const auto largest = domain - 1;
    if (largest <= std::numeric_limits<std::uint8_t>::max())
      values.emplace<0>(cells);
    else if (largest <= std::numeric_limits<std::uint16_t>::max())
      values.emplace<1>(cells);
    else if (largest <= std::numeric_limits<std::uint32_t>::max())
      values.emplace<2>(cells);
    else
      values.emplace<3>(cells);
    std::visit(fill, values);
  }

  size_t operator[](size_t cell) const noexcept {
    return std::visit(
        [cell](const auto &table) -> size_t { return table[cell]; }, values);
  }
};

class BucketKernel {
  const BucketInput &input;
  Deadline deadline;
  std::vector<double> &costs;

  void offsets(size_t cell, std::span<size_t> bases) const noexcept;
  size_t reduce(size_t cell, std::span<const size_t> bases) noexcept;

public:
  BucketKernel(const BucketInput &in, Deadline limit,
               std::vector<double> &table)
      : input(in), deadline(limit), costs(table) {}

  template <typename Choice>
  void run(size_t threads, std::span<Choice> choices);
};

void BucketKernel::offsets(size_t cell,
                           std::span<size_t> bases) const noexcept {
  std::fill(bases.begin(), bases.end(), 0);
  for (size_t pos = input.separator_domains.size(); pos-- > 0;) {
    const size_t value = cell % input.separator_domains[pos];
    cell /= input.separator_domains[pos];
    for (size_t i = 0; i < bases.size(); ++i)
      bases[i] += value * input.factors[i].separator_strides[pos];
  }
}

size_t BucketKernel::reduce(size_t cell,
                            std::span<const size_t> bases) noexcept {
  double best = PBQP_INF;
  size_t choice = 0;
  for (size_t x = 0; x < input.domain; ++x) {
    if (x && x % 256 == 0 && deadline.expired())
      return 0;
    double sum = 0;
    for (size_t i = 0; i < bases.size(); ++i) {
      const auto &factor = input.factors[i];
      sum += factor.cost(bases[i] + x * factor.stride);
      if (sum == PBQP_INF)
        break;
    }
    if (sum < best) {
      best = sum;
      choice = x;
    }
  }
  costs[cell] = best;
  return choice;
}

template <typename Choice>
void BucketKernel::run(size_t threads, std::span<Choice> choices) {
  std::vector<std::vector<size_t>> scratch(
      threads, std::vector<size_t>(input.factors.size()));
  run_parallel(threads, [&](size_t id) {
    auto &bases = scratch[id];
    const size_t width = input.cells / threads;
    const size_t extra = input.cells % threads;
    const size_t begin = width * id + std::min(id, extra);
    const size_t end = begin + width + (id < extra);
    for (size_t cell = begin; cell < end && !deadline.expired(); ++cell) {
      offsets(cell, bases);
      choices[cell] = static_cast<Choice>(reduce(cell, bases));
    }
  });
}

class BucketSearch {
  const pbqp_problem &problem;
  const BucketPlan &plan;
  Deadline deadline;
  size_t threads;
  std::vector<size_t> position;
  std::vector<std::vector<BucketFactor>> buckets;
  std::vector<BucketChoices> choices;

  void route(BucketFactor factor);
  BucketInput project(const BucketStep &step,
                      std::span<const BucketFactor> factors) const;
  bool eliminate(size_t index);
  void reconstruct(pbqp_solution &solution) const noexcept;

public:
  BucketSearch(const pbqp_problem &p, const BucketPlan &order, Deadline limit,
               size_t concurrency);
  size_t worker_count(const BucketStep &step) const noexcept;
  pbqp_status run(bool exact, pbqp_progress_callback callback, void *data,
                  SolutionPtr &out);
};

void BucketSearch::route(BucketFactor factor) {
  const auto next = std::ranges::min_element(
      factor.scope, {}, [&](size_t id) { return position[id]; });
  assert(next != factor.scope.end());
  buckets[position[*next]].push_back(std::move(factor));
}

BucketSearch::BucketSearch(const pbqp_problem &p, const BucketPlan &order,
                           Deadline limit, size_t concurrency)
    : problem(p), plan(order), deadline(limit), threads(concurrency),
      position(p.num_nodes), buckets(p.num_nodes), choices(p.num_nodes) {
  for (size_t i = 0; i < plan.steps.size(); ++i)
    position[plan.steps[i].node] = i;
  for (size_t id = 0; id < p.num_nodes && !deadline.expired(); ++id) {
    const auto &node = p.nodes[id];
    const std::span costs(node.costs, node.num_options);
    BucketFactor factor{{id}, costs, {}};
    route(std::move(factor));
  }
  for (const auto &edge : std::span(p.edges, p.num_edges)) {
    if (deadline.expired())
      break;
    const size_t rows = p.nodes[edge.src].num_options;
    const size_t columns = p.nodes[edge.tgt].num_options;
    const std::span costs(edge.costs, rows * columns);
    BucketFactor factor{{edge.src, edge.tgt}, costs, {}};
    route(std::move(factor));
  }
}

BucketInput BucketSearch::project(const BucketStep &step,
                                  std::span<const BucketFactor> factors) const {
  BucketInput result{problem.nodes[step.node].num_options, step.cells, {}, {}};
  for (size_t id : step.scope)
    result.separator_domains.push_back(problem.nodes[id].num_options);
  for (const auto &factor : factors) {
    BucketProjection projection{
        factor.input.empty() ? nullptr : factor.input.data(),
        factor.table.data(), 0, std::vector<size_t>(step.scope.size())};
    size_t stride = 1;
    for (auto id : factor.scope | std::views::reverse) {
      if (id == step.node)
        projection.stride = stride;
      else {
        const auto pos = std::ranges::lower_bound(step.scope, id);
        assert(pos != step.scope.end() && *pos == id);
        projection.separator_strides[pos - step.scope.begin()] = stride;
      }
      stride *= problem.nodes[id].num_options;
    }
    result.factors.push_back(std::move(projection));
  }
  return result;
}

size_t BucketSearch::worker_count(const BucketStep &step) const noexcept {
  constexpr size_t combinations_per_worker = 16384;
  const size_t domain = problem.nodes[step.node].num_options;
  const size_t cells_per_worker = 1 + (combinations_per_worker - 1) / domain;
  return std::min(threads, std::max(size_t{1}, step.cells / cells_per_worker));
}

bool BucketSearch::eliminate(size_t index) {
  const auto &step = plan.steps[index];
  auto &factors = buckets[index];
  BucketFactor output{step.scope, {}, std::vector<double>(step.cells)};
  const auto input = project(step, factors);
  BucketKernel kernel(input, deadline, output.table);
  choices[index].allocate(input.domain, step.cells, [&](auto &argmin) {
    kernel.run(worker_count(step), std::span(argmin));
  });
  if (deadline.expired())
    return false;
  factors.clear();
  if (step.scope.empty())
    return output.table.front() != PBQP_INF;
  route(std::move(output));
  return true;
}

void BucketSearch::reconstruct(pbqp_solution &solution) const noexcept {
  for (size_t i = plan.steps.size(); i-- > 0;) {
    const auto &step = plan.steps[i];
    size_t cell = 0;
    for (size_t id : step.scope)
      cell = cell * problem.nodes[id].num_options + solution.options[id];
    solution.options[step.node] = choices[i][cell];
  }
}

pbqp_status BucketSearch::run(bool exact, pbqp_progress_callback callback,
                              void *data, SolutionPtr &out) {
  unsigned reported_percent = 0;
  const auto size = plan.steps.size();
  bool feasible = true;
  for (size_t i = 0; i < size && !deadline.expired(); ++i) {
    feasible = eliminate(i);
    if (!feasible)
      break;
    if (callback && i + 1 < size && !deadline.expired()) {
      const auto percent = static_cast<unsigned>(
          99.0 * (static_cast<double>(i + 1) / static_cast<double>(size)));
      if (percent > reported_percent) {
        reported_percent = percent;
        callback(percent, data);
      }
    }
  }
  if (deadline.expired())
    return PBQP_TIMEOUT;
  SolutionPtr solution;
  if (feasible) {
    solution = make_solution(problem.num_nodes);
    reconstruct(*solution);
    if (!evaluate_assignment(problem, solution->options, solution->cost))
      return PBQP_INTERNAL_ERROR;
    solution->quality = exact ? PBQP_SOLUTION_OPTIMAL : PBQP_SOLUTION_FEASIBLE;
  }
  if (deadline.expired())
    return PBQP_TIMEOUT;
  if (callback)
    callback(100, data);
  out = std::move(solution);
  return feasible ? PBQP_OK : PBQP_INFEASIBLE;
}
} // namespace pbqp
