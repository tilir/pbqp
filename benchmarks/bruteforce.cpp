/*
 * Google Benchmark thread-count comparison for exhaustive tuple enumeration.
 *
 * Enumerates a fixed 20-node binary problem (2^20 tuples) using one caller and
 * TBB solver workers. Setup and an optimum check remain outside measured loops.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
#include "pbqp.h"
#include <benchmark/benchmark.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace {
using SolutionPtr =
    std::unique_ptr<pbqp_solution, decltype([](pbqp_solution *s) noexcept {
                      pbqp_solution_free(s);
                    })>;

void binary20(benchmark::State &state) {
  const pbqp_cost_t costs[] = {1, 0};
  std::vector<pbqp_node> nodes(20, pbqp_node{2, costs});
  const pbqp_problem problem{nodes.size(), 0, nodes.data(), nullptr};
  pbqp_option options_entries[] = {
      pbqp_size_option("num-threads", static_cast<size_t>(state.range(0))),
      pbqp_progress_option("progress", nullptr),
      pbqp_pointer_option("progress-data", nullptr),
      pbqp_diagnostic_option("diagnostics", nullptr),
      pbqp_pointer_option("diagnostic-data", nullptr)};
  const pbqp_solve_options options{5, options_entries};
  pbqp_error error{};
  pbqp_solution *raw = nullptr;
  auto status = pbqp_solve_bruteforce(&problem, &options, &raw, &error);
  SolutionPtr checked(raw);
  if (status != PBQP_OK) {
    state.SkipWithError(error.message);
    return;
  }
  pbqp_evaluation evaluation{};
  if (checked->quality != PBQP_SOLUTION_OPTIMAL || checked->cost != 0 ||
      pbqp_evaluate_assignment(&problem, checked->options, checked->num_nodes,
                               &evaluation, &error) != PBQP_OK ||
      !evaluation.is_feasible || evaluation.cost != 0) {
    state.SkipWithError("invalid binary problem optimum");
    return;
  }
  checked.reset();
  for (auto iteration : state) {
    status = pbqp_solve_bruteforce(&problem, &options, &raw, &error);
    SolutionPtr solution(raw);
    if (status != PBQP_OK) {
      state.SkipWithError(error.message);
      break;
    }
    benchmark::DoNotOptimize(solution->cost);
  }
  constexpr std::int64_t tuples = std::int64_t{1} << 20;
  state.SetItemsProcessed(state.iterations() * tuples);
  state.counters["nodes"] = 20;
}
} // namespace

int main(int argc, char **argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
    benchmark::Shutdown();
    return 1;
  }
  benchmark::RegisterBenchmark("Naive/Binary20", binary20)
      ->ArgName("threads")
      ->Arg(1)
      ->Arg(2)
      ->Arg(4)
      ->Arg(8)
      ->Unit(benchmark::kMillisecond)
      ->UseRealTime();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
