/*
 * Google Benchmark coloring and B&B root seeding on a generated regalloc graph.
 * Generation and preflight checks are untimed; each measured solve includes
 * public validation, preparation, search, and returned-solution cleanup.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
#include "pbqp.h"
#include <benchmark/benchmark.h>
#include <memory>
import pbqp.generator;

namespace {
using SolutionPtr =
    std::unique_ptr<pbqp_solution, decltype([](pbqp_solution *s) noexcept {
                      pbqp_solution_free(s);
                    })>;

void coloring(benchmark::State &state, pbqp_solver solver) {
  pbqp_tools::GeneratorOptions config{180, 10000, 0, 1};
  config.family = pbqp_tools::GeneratorFamily::RegAlloc;
  config.num_registers = 32;
  const auto generated = pbqp_tools::generate_problem(config);
  const auto problem = generated.view();
  const pbqp_option entries[] = {pbqp_size_option("optimization-level", 2),
                                 pbqp_bool_option("dsatur-seed", 1)};
  const pbqp_solve_options options{2, entries};
  const auto *selected = solver == PBQP_SOLVER_BNB ? &options : nullptr;
  pbqp_error error;
  pbqp_solution *raw = nullptr;
  if (pbqp_solve(solver, &problem, selected, &raw, &error) != PBQP_OK) {
    state.SkipWithError(error.message);
    return;
  }
  SolutionPtr checked(raw);
  pbqp_evaluation evaluation{};
  const auto quality = solver == PBQP_SOLVER_BNB ? PBQP_SOLUTION_OPTIMAL
                                                 : PBQP_SOLUTION_FEASIBLE;
  if (checked->quality != quality || checked->cost != 0 ||
      pbqp_evaluate_assignment(&problem, checked->options, checked->num_nodes,
                               &evaluation, &error) != PBQP_OK ||
      !evaluation.is_feasible || evaluation.cost != 0) {
    state.SkipWithError("invalid coloring witness");
    return;
  }
  checked.reset();
  for (auto iteration : state) {
    const auto status = pbqp_solve(solver, &problem, selected, &raw, &error);
    SolutionPtr solution(raw);
    if (status != PBQP_OK) {
      state.SkipWithError(error.message);
      break;
    }
    benchmark::DoNotOptimize(solution->cost);
  }
  state.counters["nodes"] = static_cast<double>(problem.num_nodes);
  state.counters["edges"] = static_cast<double>(problem.num_edges);
  state.counters["registers"] = static_cast<double>(config.num_registers);
  state.SetItemsProcessed(state.iterations());
}
} // namespace

int main(int argc, char **argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv))
    return 1;
  benchmark::RegisterBenchmark("ColoringSeed/Regalloc180/Dsatur", coloring,
                               PBQP_SOLVER_DSATUR)
      ->UseRealTime();
  benchmark::RegisterBenchmark("ColoringSeed/Regalloc180/BnbO2", coloring,
                               PBQP_SOLVER_BNB)
      ->UseRealTime();
  benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
}
