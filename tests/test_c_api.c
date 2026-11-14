/*
 * C compatibility and linkage test.
 *
 * Includes the public header from C and exercises cost arithmetic and solving.
 * Checks that a C caller can use the C++ implementation through the C API.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
#include "pbqp.h"

typedef struct progress_log {
  unsigned count;
  int invalid;
} progress_log;

static void record_progress(unsigned percent, void *data) {
  progress_log *log = (progress_log *)data;
  if (percent != log->count + 1 || percent > 100)
    log->invalid = 1;
  ++log->count;
}

typedef struct diagnostic_log {
  unsigned count;
  int invalid;
} diagnostic_log;

static void record_diagnostic(const char *message, void *data) {
  diagnostic_log *log = (diagnostic_log *)data;
  if (!message || !message[0])
    log->invalid = 1;
  ++log->count;
}

/* Exercise both dedicated entries and enum dispatch from an actual C caller. */
static int test_dispatch(const pbqp_problem *problem) {
  typedef pbqp_status (*solver_fn)(const pbqp_problem *,
                                   const pbqp_solve_options *, pbqp_solution **,
                                   pbqp_error *);

  const struct {
    pbqp_solver kind;
    solver_fn solve;
    int exact;
  } solvers[] = {{PBQP_SOLVER_NAIVE, pbqp_solve_bruteforce, 1},
                 {PBQP_SOLVER_BNB, pbqp_solve_branch_and_bound, 1},
                 {PBQP_SOLVER_GREEDY, pbqp_solve_greedy, 0},
                 {PBQP_SOLVER_R1R2RN, pbqp_solve_reduction, 0}};

  size_t i;
  for (i = 0; i < sizeof(solvers) / sizeof(solvers[0]); ++i) {
    progress_log progress = {0, 0};
    diagnostic_log diagnostics = {0, 0};
    pbqp_option options_entries[] = {
        pbqp_size_option("num-threads", 4),
        pbqp_progress_option("progress",
                             solvers[i].exact ? record_progress : NULL),
        pbqp_pointer_option("progress-data", &progress),
        pbqp_diagnostic_option("diagnostics", record_diagnostic),
        pbqp_pointer_option("diagnostic-data", &diagnostics)};
    pbqp_solve_options options = {5, options_entries};
    pbqp_solution *direct = NULL, *dispatched = NULL;
    pbqp_error error;
    if (solvers[i].solve(problem, &options, &direct, &error) != PBQP_OK)
      return 0;
    pbqp_cost_t cost = direct->cost;
    pbqp_solution_quality quality = direct->quality;
    pbqp_solution_free(direct);
    if (cost != -2 ||
        quality != (solvers[i].exact ? PBQP_SOLUTION_OPTIMAL
                                     : PBQP_SOLUTION_FEASIBLE) ||
        diagnostics.count == 0 || diagnostics.invalid || progress.invalid ||
        progress.count != (solvers[i].exact ? 100u : 0u) || error.line ||
        error.message[0])
      return 0;
    progress.count = diagnostics.count = 0;
    if (pbqp_solve(solvers[i].kind, problem, &options, &dispatched, &error) !=
        PBQP_OK)
      return 0;
    int matches = dispatched->cost == cost && dispatched->quality == quality;
    pbqp_solution_free(dispatched);
    if (!matches || diagnostics.count == 0 || diagnostics.invalid ||
        progress.invalid || progress.count != (solvers[i].exact ? 100u : 0u))
      return 0;
    if (pbqp_solve(solvers[i].kind, problem, NULL, &dispatched, NULL) !=
        PBQP_OK)
      return 0;
    pbqp_solution_free(dispatched);
    progress.count = diagnostics.count = 0;
    options_entries[0].value.size = 0;
    if (pbqp_solve(solvers[i].kind, problem, &options, &dispatched, &error) !=
            PBQP_INVALID ||
        dispatched || progress.count || diagnostics.count || !error.message[0])
      return 0;
    options_entries[0].value.size = 1;
    if (pbqp_solve(solvers[i].kind, NULL, &options, &dispatched, &error) !=
            PBQP_INVALID ||
        dispatched || progress.count || diagnostics.count)
      return 0;
    if (pbqp_solve(solvers[i].kind, problem, &options, NULL, &error) !=
            PBQP_INVALID ||
        progress.count || diagnostics.count)
      return 0;
    if (!solvers[i].exact) {
      options_entries[1].value.progress = record_progress;
      pbqp_option strict_entries[6];
      size_t entry;
      for (entry = 0; entry < 5; ++entry)
        strict_entries[entry] = options_entries[entry];
      strict_entries[5] = pbqp_bool_option("warnings-as-errors", 1);
      const pbqp_solve_options strict = {6, strict_entries};
      if (solvers[i].solve(problem, &strict, &dispatched, &error) !=
              PBQP_INVALID ||
          dispatched || progress.count || diagnostics.count ||
          !error.message[0])
        return 0;
    }
  }
  {
    progress_log progress = {0, 0};
    diagnostic_log diagnostics = {0, 0};
    pbqp_option options_entries[] = {
        pbqp_size_option("num-threads", 1),
        pbqp_progress_option("progress", record_progress),
        pbqp_pointer_option("progress-data", &progress),
        pbqp_diagnostic_option("diagnostics", record_diagnostic),
        pbqp_pointer_option("diagnostic-data", &diagnostics)};
    pbqp_solve_options options = {5, options_entries};
    pbqp_solution *solution = NULL;
    pbqp_error error;
    if (pbqp_solve((pbqp_solver)7, problem, &options, &solution, &error) !=
            PBQP_INVALID ||
        solution || !error.message[0] || error.line || progress.count ||
        diagnostics.count)
      return 0;
    if (pbqp_solve((pbqp_solver)7, problem, NULL, NULL, NULL) != PBQP_INVALID)
      return 0;
  }
  return 1;
}

/* The open map is borrowed, typed, extensible, and last-wins. */
static int test_option_map(void) {
  const pbqp_problem empty = {0, 0, NULL, NULL};
  pbqp_solution *solution = NULL;
  pbqp_error error;
  pbqp_option entries[] = {pbqp_string_option("application.setting", NULL),
                           pbqp_bool_option("num-threads", 1),
                           pbqp_size_option("num-threads", 1),
                           pbqp_size_option("optimization-level", 99),
                           pbqp_size_option("optimization-level", 0),
                           pbqp_bool_option("greedy-seed", 1),
                           pbqp_bool_option("greedy-seed", 0)};
  pbqp_solve_options options = {7, entries};
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, &error) !=
          PBQP_OK ||
      !solution || error.line || error.message[0])
    return 0;
  pbqp_solution_free(solution);

  entries[2] = pbqp_bool_option("num-threads", 1);
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, &error) !=
          PBQP_INVALID ||
      solution || !error.message[0])
    return 0;
  entries[2] = pbqp_size_option("num-threads", 1);
  entries[6] = pbqp_bool_option("greedy-seed", 2);
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, NULL) !=
          PBQP_INVALID ||
      solution)
    return 0;
  /* Naive does not consume B&B-specific values, even malformed ones. */
  if (pbqp_solve_bruteforce(&empty, &options, &solution, &error) != PBQP_OK ||
      !solution || error.message[0])
    return 0;
  pbqp_solution_free(solution);
  entries[6] = pbqp_bool_option("greedy-seed", 0);
  entries[4] = pbqp_size_option("optimization-level", 3);
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, NULL) !=
          PBQP_INVALID ||
      solution)
    return 0;
  entries[4] = pbqp_size_option("optimization-level", 2);
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, NULL) != PBQP_OK)
    return 0;
  pbqp_solution_free(solution);
  entries[0].key = NULL;
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, NULL) !=
          PBQP_INVALID ||
      solution)
    return 0;
  entries[0].key = "";
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, NULL) !=
          PBQP_INVALID ||
      solution)
    return 0;
  entries[0].key = "application.setting";
  entries[0].type = 99; /* Unknown keys do not inspect even the value tag. */
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, NULL) != PBQP_OK)
    return 0;
  pbqp_solution_free(solution);
  entries[6].type = 99;
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, NULL) !=
          PBQP_INVALID ||
      solution)
    return 0;
  options.entries = NULL;
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, NULL) !=
          PBQP_INVALID ||
      solution)
    return 0;
  options.count = 0;
  options.entries = entries; /* Even invalid entries are unread at zero size. */
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, NULL) != PBQP_OK)
    return 0;
  pbqp_solution_free(solution);
  return 1;
}

static int test_unknown_option_warnings(void) {
  const pbqp_problem empty = {0, 0, NULL, NULL};
  diagnostic_log warnings = {0, 0}, diagnostics = {0, 0};
  pbqp_option entries[] = {
      pbqp_diagnostic_option("warnings", record_diagnostic),
      pbqp_pointer_option("warning-data", &warnings),
      pbqp_diagnostic_option("diagnostics", record_diagnostic),
      pbqp_pointer_option("diagnostic-data", &diagnostics),
      pbqp_bool_option("warnings-as-errors", 0),
      pbqp_string_option("application.setting", NULL),
      pbqp_string_option("application.setting", NULL)};
  const pbqp_solve_options options = {7, entries};
  pbqp_solution *solution = NULL;
  pbqp_error error;
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, &error) !=
          PBQP_OK ||
      !solution || warnings.count != 1 || warnings.invalid ||
      !diagnostics.count || error.line || error.message[0])
    return 0;
  pbqp_solution_free(solution);
  warnings.count = diagnostics.count = 0;
  entries[4] = pbqp_bool_option("warnings-as-errors", 1);
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, &error) !=
          PBQP_INVALID ||
      solution || warnings.count || diagnostics.count || !error.message[0] ||
      error.line)
    return 0;
  /* Strict mode also works without any warning sink. */
  entries[0] = pbqp_diagnostic_option("warnings", NULL);
  entries[2] = pbqp_diagnostic_option("diagnostics", NULL);
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, NULL) !=
          PBQP_INVALID ||
      solution)
    return 0;
  /* A B&B setting is understood by B&B but warns on naive. */
  entries[5] = pbqp_bool_option("weighted-bounds", 0);
  entries[6] = pbqp_bool_option("weighted-bounds", 1);
  if (pbqp_solve_branch_and_bound(&empty, &options, &solution, NULL) != PBQP_OK)
    return 0;
  pbqp_solution_free(solution);
  if (pbqp_solve_bruteforce(&empty, &options, &solution, NULL) !=
          PBQP_INVALID ||
      solution)
    return 0;
  entries[0] = pbqp_diagnostic_option("warnings", record_diagnostic);
  entries[1] = pbqp_pointer_option("warning-data", &warnings);
  entries[4] = pbqp_bool_option("warnings-as-errors", 0);
  if (pbqp_solve_bruteforce(&empty, &options, &solution, &error) != PBQP_OK ||
      !solution || warnings.count != 1 || error.message[0])
    return 0;
  pbqp_solution_free(solution);
  /* The diagnostic sink is the fallback when no warning sink is installed. */
  warnings.count = diagnostics.count = 0;
  entries[0] = pbqp_diagnostic_option("warnings", NULL);
  entries[2] = pbqp_diagnostic_option("diagnostics", record_diagnostic);
  if (pbqp_solve_bruteforce(&empty, &options, &solution, NULL) != PBQP_OK ||
      warnings.count || diagnostics.count < 2)
    return 0;
  pbqp_solution_free(solution);
  return 1;
}

static int test_unsupported_heuristic_options(void) {
  const pbqp_problem empty = {0, 0, NULL, NULL};
  typedef pbqp_status (*solver_fn)(const pbqp_problem *,
                                   const pbqp_solve_options *, pbqp_solution **,
                                   pbqp_error *);
  solver_fn solvers[] = {pbqp_solve_greedy, pbqp_solve_reduction};
  size_t index;
  for (index = 0; index < 2; ++index) {
    progress_log progress = {0, 0};
    diagnostic_log warnings = {0, 0}, diagnostics = {0, 0};
    pbqp_option entries[] = {
        pbqp_size_option("num-threads", 4),
        pbqp_progress_option("progress", record_progress),
        pbqp_pointer_option("progress-data", &progress),
        pbqp_diagnostic_option("warnings", record_diagnostic),
        pbqp_pointer_option("warning-data", &warnings),
        pbqp_diagnostic_option("diagnostics", record_diagnostic),
        pbqp_pointer_option("diagnostic-data", &diagnostics),
        pbqp_bool_option("warnings-as-errors", 0)};
    const pbqp_solve_options options = {8, entries};
    pbqp_solution *solution = NULL;
    pbqp_error error;
    if (solvers[index](&empty, &options, &solution, &error) != PBQP_OK ||
        !solution || solution->quality != PBQP_SOLUTION_FEASIBLE ||
        warnings.count != 2 || warnings.invalid || !diagnostics.count ||
        progress.count || error.message[0])
      return 0;
    pbqp_solution_free(solution);
    warnings.count = diagnostics.count = 0;
    entries[7] = pbqp_bool_option("warnings-as-errors", 1);
    if (solvers[index](&empty, &options, &solution, &error) != PBQP_INVALID ||
        solution || warnings.count || diagnostics.count || progress.count ||
        !error.message[0])
      return 0;
    entries[0] = pbqp_size_option("num-threads", 1);
    entries[1] = pbqp_progress_option("progress", NULL);
    entries[7] = pbqp_bool_option("warnings-as-errors", 0);
    entries[2] = pbqp_bool_option("unrecognized", 1);
    if (solvers[index](&empty, &options, &solution, NULL) != PBQP_OK ||
        !solution || warnings.count != 1 || progress.count)
      return 0;
    pbqp_solution_free(solution);
  }
  return 1;
}

int main(void) {
  pbqp_api_version version;
  if (!test_option_map() || !test_unknown_option_warnings() ||
      !test_unsupported_heuristic_options())
    return 1;
  pbqp_get_api_version(NULL);
  pbqp_get_api_version(&version);
  if (version.major != PBQP_API_VERSION_MAJOR ||
      version.minor != PBQP_API_VERSION_MINOR ||
      version.patch != PBQP_API_VERSION_PATCH || version.major != 2 ||
      version.minor != 0 || version.patch != 0)
    return 1;
  pbqp_cost_t costs[] = {4, -2};
  pbqp_node node = {2, costs};
  pbqp_problem problem = {1, 0, &node, NULL};
  pbqp_solution *solution = NULL;
  int ok;
  if (pbqp_cost_add(PBQP_INF, -1.0f) != PBQP_INF)
    return 1;
  if (pbqp_cost_add(0.5f, -0.75f) != -0.25f)
    return 1;
  if (pbqp_solve_bruteforce(&problem, NULL, &solution, NULL) != PBQP_OK)
    return 1;
  ok = solution->quality == PBQP_SOLUTION_OPTIMAL && solution->cost == -2 &&
       solution->options[0] == 1;
  pbqp_solution_free(solution);
  if (!ok || pbqp_solve_greedy(&problem, NULL, &solution, NULL) != PBQP_OK)
    return 1;
  {
    pbqp_evaluation evaluation;
    ok = solution->quality == PBQP_SOLUTION_FEASIBLE &&
         pbqp_evaluate_assignment(&problem, solution->options,
                                  solution->num_nodes, &evaluation,
                                  NULL) == PBQP_OK &&
         evaluation.is_feasible && evaluation.cost == -2;
  }
  pbqp_solution_free(solution);
  if (!ok ||
      pbqp_solve_branch_and_bound(&problem, NULL, &solution, NULL) != PBQP_OK)
    return 1;
  ok = solution->quality == PBQP_SOLUTION_OPTIMAL && solution->cost == -2 &&
       solution->options[0] == 1;
  pbqp_solution_free(solution);
  if (!ok || pbqp_solve_reduction(&problem, NULL, &solution, NULL) != PBQP_OK)
    return 1;
  ok = solution->quality == PBQP_SOLUTION_FEASIBLE && solution->cost == -2 &&
       solution->options[0] == 1;
  pbqp_solution_free(solution);
  {
    progress_log log = {0, 0};
    pbqp_option options_entries[] = {
        pbqp_size_option("num-threads", 1),
        pbqp_progress_option("progress", record_progress),
        pbqp_pointer_option("progress-data", &log),
        pbqp_diagnostic_option("diagnostics", NULL),
        pbqp_pointer_option("diagnostic-data", NULL)};
    const pbqp_solve_options options = {5, options_entries};
    if (pbqp_solve_bruteforce(&problem, &options, &solution, NULL) != PBQP_OK)
      return 1;
    ok = ok && log.count == 100 && !log.invalid && solution->cost == -2;
    pbqp_solution_free(solution);
  }
  {
    progress_log log = {0, 0};
    pbqp_option options_entries[] = {
        pbqp_size_option("num-threads", 1),
        pbqp_progress_option("progress", record_progress),
        pbqp_pointer_option("progress-data", &log),
        pbqp_diagnostic_option("diagnostics", NULL),
        pbqp_pointer_option("diagnostic-data", NULL)};
    const pbqp_solve_options options = {5, options_entries};
    if (pbqp_solve_branch_and_bound(&problem, &options, &solution, NULL) !=
        PBQP_OK)
      return 1;
    ok = ok && log.count == 100 && !log.invalid && solution->cost == -2;
    pbqp_solution_free(solution);
  }
  {
    progress_log log = {0, 0};
    pbqp_option options_entries[] = {
        pbqp_size_option("num-threads", 4),
        pbqp_progress_option("progress", record_progress),
        pbqp_pointer_option("progress-data", &log),
        pbqp_diagnostic_option("diagnostics", NULL),
        pbqp_pointer_option("diagnostic-data", NULL)};
    pbqp_solve_options options = {5, options_entries};
    if (pbqp_solve_branch_and_bound(&problem, &options, &solution, NULL) !=
        PBQP_OK)
      return 1;
    ok = ok && solution->quality == PBQP_SOLUTION_OPTIMAL &&
         solution->cost == -2 && log.count == 100 && !log.invalid;
    pbqp_solution_free(solution);
    log.count = 0;
    log.invalid = 0;
    if (pbqp_solve_bruteforce(&problem, &options, &solution, NULL) != PBQP_OK)
      return 1;
    ok = ok && solution->cost == -2 && solution->options[0] == 1 &&
         log.count == 100 && !log.invalid;
    pbqp_solution_free(solution);
    options_entries[0].value.size = 0;
    if (pbqp_solve_branch_and_bound(&problem, &options, &solution, NULL) !=
            PBQP_INVALID ||
        solution)
      return 1;
  }
  if (pbqp_solve_branch_and_bound(&problem, NULL, &solution, NULL) != PBQP_OK)
    return 1;
  ok = ok && solution->quality == PBQP_SOLUTION_OPTIMAL && solution->cost == -2;
  pbqp_solution_free(solution);
  {
    progress_log log = {0, 0};
    pbqp_option options_entries[] = {
        pbqp_size_option("num-threads", 4),
        pbqp_progress_option("progress", record_progress),
        pbqp_pointer_option("progress-data", &log),
        pbqp_diagnostic_option("diagnostics", NULL),
        pbqp_pointer_option("diagnostic-data", NULL)};
    pbqp_solve_options options = {5, options_entries};
    if (pbqp_solve_branch_and_bound(&problem, &options, &solution, NULL) !=
        PBQP_OK)
      return 1;
    ok = ok && solution->cost == -2 && log.count == 100 && !log.invalid;
    pbqp_solution_free(solution);
    log.count = 0;
    options_entries[0].value.size = 1;
    if (pbqp_solve_branch_and_bound(&problem, &options, &solution, NULL) !=
        PBQP_OK)
      return 1;
    ok = ok && log.count == 100 && !log.invalid;
    pbqp_solution_free(solution);
  }
  {
    pbqp_cost_t unary[] = {3, 0};
    pbqp_cost_t pair[] = {0, 0, 0, PBQP_INF};
    pbqp_node nodes[] = {{2, unary}, {2, unary}, {2, unary}};
    pbqp_edge edges[] = {{0, 1, pair}, {0, 2, pair}, {1, 2, pair}};
    pbqp_problem regalloc = {3, 3, nodes, edges};
    if (pbqp_solve_branch_and_bound(&regalloc, NULL, &solution, NULL) !=
        PBQP_OK)
      return 1;
    ok =
        ok && solution->quality == PBQP_SOLUTION_OPTIMAL && solution->cost == 6;
    pbqp_solution_free(solution);
  }
  {
    progress_log progress = {0, 0};
    diagnostic_log diagnostics = {0, 0};
    pbqp_option options_entries[] = {
        pbqp_size_option("num-threads", 4),
        pbqp_progress_option("progress", record_progress),
        pbqp_pointer_option("progress-data", &progress),
        pbqp_diagnostic_option("diagnostics", record_diagnostic),
        pbqp_pointer_option("diagnostic-data", &diagnostics)};
    pbqp_solve_options options = {5, options_entries};
    if (pbqp_solve_branch_and_bound(&problem, &options, &solution, NULL) !=
        PBQP_OK)
      return 1;
    ok = ok && solution->cost == -2 && progress.count == 100 &&
         !progress.invalid && diagnostics.count > 0 && !diagnostics.invalid;
    pbqp_solution_free(solution);
    diagnostics.count = 0;
    if (pbqp_solve_branch_and_bound(NULL, &options, &solution, NULL) !=
            PBQP_INVALID ||
        solution || diagnostics.count)
      return 1;
  }
  return ok && test_dispatch(&problem) ? 0 : 1;
}
