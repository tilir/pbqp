/*
 * PBQP format, arithmetic, solver, and generator tests.
 *
 * Checks examples, malformed input, round trips, and numeric boundaries.
 * Compares generated instances against an independent recursive reference.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
#include "pbqp.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <source_location>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import pbqp.generator;

namespace {
std::string case_context;

void check(bool condition,
           std::source_location where = std::source_location::current()) {
  if (!condition) {
    std::ostringstream message;
    message << where.file_name() << ':' << where.line() << " in "
            << where.function_name() << ": PBQP check failed";
    if (!case_context.empty())
      message << " (" << case_context << ')';
    throw std::runtime_error(message.str());
  }
}

class TemporaryFile {
  std::filesystem::path path;

public:
  TemporaryFile() {
    static size_t sequence = 0;
    auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    path = std::filesystem::temp_directory_path() /
           ("pbqp-test-" + std::to_string(stamp) + "-" +
            std::to_string(sequence++));
  }
  std::string name() const { return path.string(); }
  ~TemporaryFile() {
    std::error_code error;
    std::filesystem::remove(path, error);
  }
};

pbqp_status read_text(const std::string &text, pbqp_problem **p,
                      pbqp_error *error) {
  TemporaryFile file;
  std::ofstream output(file.name(), std::ios::binary);
  output << text;
  output.close();
  check(static_cast<bool>(output));
  return pbqp_read(file.name().c_str(), p, error);
}

void same_problem(const pbqp_problem *a, const pbqp_problem *b) {
  size_t i, j;
  check(a->num_nodes == b->num_nodes && a->num_edges == b->num_edges);
  for (i = 0; i < a->num_nodes; ++i) {
    check(a->nodes[i].num_options == b->nodes[i].num_options);
    for (j = 0; j < a->nodes[i].num_options; ++j)
      check(a->nodes[i].costs[j] == b->nodes[i].costs[j]);
  }
  for (i = 0; i < a->num_edges; ++i) {
    check(a->edges[i].src == b->edges[i].src &&
          a->edges[i].tgt == b->edges[i].tgt);
    for (j = 0; j < a->nodes[a->edges[i].src].num_options *
                        a->nodes[a->edges[i].tgt].num_options;
         ++j)
      check(a->edges[i].costs[j] == b->edges[i].costs[j]);
  }
}

void check_roundtrip(const pbqp_problem *p) {
  TemporaryFile file;
  pbqp_problem *copy = nullptr;
  pbqp_error error;
  check(pbqp_write(file.name().c_str(), p, &error) == PBQP_OK);
  check(pbqp_read(file.name().c_str(), &copy, &error) == PBQP_OK);
  same_problem(p, copy);
  pbqp_problem_free(copy);
}

void test_examples() {
  constexpr const char *paths[] = {"examples/01_basic.pbqp",
                                   "examples/02_greedy_exact.pbqp",
                                   "examples/03_greedy_inexact.pbqp"};
  const pbqp_cost_t expected[] = {0, 40, 80};
  const pbqp_cost_t greedy_expected[] = {0, 40, 130};
  for (size_t k = 0; k < 3; ++k) {
    pbqp_problem *p = nullptr;
    pbqp_solution *s = nullptr;
    pbqp_error error;
    check(pbqp_read(paths[k], &p, &error) == PBQP_OK);
    check_roundtrip(p);
    check(pbqp_solve_bruteforce(p, &s, &error) == PBQP_OK);
    check(s->cost == expected[k]);
    check(s->quality == PBQP_SOLUTION_OPTIMAL);
    pbqp_solution_free(s);
    check(pbqp_solve_branch_and_bound(p, &s, &error) == PBQP_OK);
    check(s->cost == expected[k] && s->quality == PBQP_SOLUTION_OPTIMAL);
    pbqp_solution_free(s);
    check(pbqp_solve_greedy(p, &s, &error) == PBQP_OK);
    check(s->quality == PBQP_SOLUTION_FEASIBLE &&
          s->cost == greedy_expected[k]);
    if (k == 1) {
      check(s->options[4] == 0);
      check(s->options[1] != 0 && s->options[3] != 0 && s->options[5] != 0);
      check(s->options[1] != s->options[3] && s->options[1] != s->options[5] &&
            s->options[3] != s->options[5]);
    }
    pbqp_solution_free(s);
    pbqp_problem_free(p);
  }
}

void test_algebra() {
  check(pbqp_cost_min(PBQP_INF, -3.5f) == -3.5f);
  check(pbqp_cost_min(PBQP_INF, PBQP_INF) == PBQP_INF);
  check(pbqp_cost_add(0.5f, -0.75f) == -0.25f);
  check(pbqp_cost_add(PBQP_INF, -1.0f) == PBQP_INF);
  check(pbqp_cost_add(PBQP_INF, PBQP_INF) == PBQP_INF);
  check(std::isnan(pbqp_cost_add(PBQP_INF, -PBQP_INF)));
  const auto maximum = std::numeric_limits<pbqp_cost_t>::max();
  check(pbqp_cost_add(maximum, maximum) == PBQP_INF);
  check(pbqp_cost_add(-maximum, -maximum) == -PBQP_INF);
  check(pbqp_cost_add(maximum, -maximum) == 0.0f);
  const pbqp_cost_t a = 0.1f, b = 0.2f;
  check(pbqp_cost_add(a, b) == a + b);
}

void solve_text(const std::string &text, pbqp_status expected, pbqp_cost_t cost,
                const size_t *options) {
  pbqp_problem *p = nullptr;
  pbqp_solution *s = nullptr;
  size_t k;
  check(read_text(text, &p, nullptr) == PBQP_OK);
  check_roundtrip(p);
  for (auto solve : {pbqp_solve_bruteforce, pbqp_solve_branch_and_bound}) {
    check(solve(p, &s, nullptr) == expected);
    if (expected == PBQP_OK) {
      check(s && s->cost == cost && s->num_nodes == p->num_nodes);
      check(s->quality == PBQP_SOLUTION_OPTIMAL);
      for (k = 0; solve == pbqp_solve_bruteforce && options && k < s->num_nodes;
           ++k)
        check(s->options[k] == options[k]);
    } else
      check(!s);
    pbqp_solution_free(s);
  }
  pbqp_problem_free(p);
}

void test_solver() {
  const size_t rectangular[] = {1, 2};
  const size_t tie[] = {0, 0};
  solve_text("#comment\nPBQP\t1\r\nNODES 0 EDGES 0 END#eof", PBQP_OK, 0,
             nullptr);
  solve_text("PBQP 1 NODES 2 EDGES 0 NODE 0 2 0 0 NODE 1 2 0 0 END", PBQP_OK, 0,
             tie);
  solve_text("PBQP 1 NODES 1 EDGES 0 NODE 0 1 " + std::string(512, '0') +
                 "1 END",
             PBQP_OK, 1, nullptr);
  solve_text("PBQP 1 NODES 2 EDGES 1 NODE 0 2 -1 +3 NODE 1 3 0 INF 2 "
             "EDGE 0 1 5 0 INF 1 0 -2 END",
             PBQP_OK, 3, rectangular);
  solve_text("PBQP 1 NODES 1 EDGES 0 NODE 0 2 INF INF END", PBQP_INFEASIBLE, 0,
             nullptr);
  solve_text("PBQP 1 NODES 2 EDGES 1 NODE 0 1 0 NODE 1 1 0 EDGE 0 1 INF END",
             PBQP_INFEASIBLE, 0, nullptr);
  auto single_options = [](std::initializer_list<pbqp_cost_t> costs) {
    std::ostringstream text;
    text << std::setprecision(std::numeric_limits<pbqp_cost_t>::max_digits10);
    text << "PBQP 1 NODES " << costs.size() << " EDGES 0 ";
    size_t i = 0;
    for (pbqp_cost_t c : costs) {
      text << "NODE " << i++ << " 1 ";
      if (c == PBQP_INF)
        text << "INF ";
      else
        text << c << ' ';
    }
    return text.str() + "END";
  };
  solve_text("PBQP 1 NODES 2 EDGES 1 NODE 0 2 0.5 1.25 NODE 1 2 -2.5 1e-1 "
             "EDGE 0 1 0.25 INF -1.5 0 END",
             PBQP_OK, -2.75f, nullptr);
  const auto maximum = std::numeric_limits<pbqp_cost_t>::max();
  solve_text(single_options({maximum, maximum}), PBQP_OK, PBQP_INF, nullptr);
  solve_text(single_options({-maximum, -maximum}), PBQP_OK, -PBQP_INF, nullptr);
  solve_text(single_options({maximum, maximum, -maximum}), PBQP_OK, PBQP_INF,
             nullptr);
  solve_text(single_options({maximum, -maximum, maximum}), PBQP_OK, maximum,
             nullptr);
  solve_text(single_options({maximum, maximum, PBQP_INF}), PBQP_INFEASIBLE, 0,
             nullptr);
  solve_text(
      single_options({std::numeric_limits<pbqp_cost_t>::denorm_min(), 0.125f}),
      PBQP_OK, 0.125f, nullptr);
  solve_text(single_options({0.123456789f, -0.000000123456789f}), PBQP_OK,
             pbqp_cost_add(0.123456789f, -0.000000123456789f), nullptr);
}

void test_invalid() {
  const char *cases[] = {
      "",
      "PBQP 2 NODES 0 EDGES 0 END",
      "PBQP 1 EDGES 0 NODES 0 END",
      "PBQP 1 NODES -1 EDGES 0 END",
      "PBQP 1 NODES 1 EDGES 0 NODE 1 1 0 END",
      "PBQP 1 NODES 1 EDGES 0 NODE 0 0 END",
      "PBQP 1 NODES 1 EDGES 0 NODE 0 1 inf END",
      "PBQP 1 NODES 1 EDGES 0 NODE 0 1 0x10 END",
      "PBQP 1 NODES 1 EDGES 0 NODE 0 1 + END",
      "PBQP 1 NODES 1 EDGES 0 NODE 0 1 +-1 END",
      "PBQP 1 NODES 1 EDGES 0 NODE 0 1 +-0.5 END",
      "PBQP 1 NODES 1 EDGES 0 NODE 0 1 +-1e2 END",
      "PBQP 1 NODES 1 EDGES 0 NODE 0 1 ++1 END",
      "PBQP 1 NODES 1 EDGES 0 NODE 0 2 1 END",
      "PBQP 1 NODES 1 EDGES 1 NODE 0 1 0 EDGE 0 0 0 END",
      "PBQP 1 NODES 2 EDGES 1 NODE 0 1 0 NODE 1 1 0 EDGE 1 0 0 END",
      "PBQP 1 NODES 2 EDGES 1 NODE 0 1 0 NODE 1 1 0 EDGE 0 2 0 END",
      "PBQP 1 NODES 2 EDGES 2 NODE 0 1 0 NODE 1 1 0 EDGE 0 1 0 EDGE 0 1 0 END",
      "PBQP 1 NODES 0 EDGES 0",
      "PBQP 1 NODES 0 EDGES 0 END garbage",
      "PBQP 1 NODES 1 EDGES 0 NODE 0 1 nan END",
      "PBQP 1 NODES 1 EDGES 0 NODE 0 1 -INF END",
      "PBQP 1 NODES 1 EDGES 0 NODE 0 1 +INF END",
      "PBQP 1 NODES 1 EDGES 0 NODE 0 1 1e END",
      "PBQP 1 NODES 1 EDGES 0 NODE 0 1 1,5 END"};
  size_t k;
  pbqp_problem *p = nullptr;
  pbqp_error error;
  for (k = 0; k < sizeof(cases) / sizeof(cases[0]); ++k) {
    check(read_text(cases[k], &p, &error) == PBQP_INVALID);
    check(!p && error.line && error.message[0]);
  }
  check(read_text("PBQP 1 NODES 1 EDGES 0 NODE 0 1 1e100 END", &p, &error) ==
            PBQP_OVERFLOW &&
        !p);
  check(read_text("PBQP 1 NODES 999999999999999999999999 EDGES 0 END", &p,
                  &error) == PBQP_OVERFLOW);
  check(read_text("PBQP 1 NODES 1 EDGES 0 NODE 0 1 -1e100 END", &p, &error) ==
        PBQP_OVERFLOW);
  check(read_text("PBQP 1\nNODES 1\nEDGES 0\nNODE 0 0\nEND", &p, &error) ==
        PBQP_INVALID);
  check(error.line == 4);
  check(read_text("PBQP 1\nNODES 1\nEDGES 0\nNODE 0 1\n+-1\nEND", &p, &error) ==
        PBQP_INVALID);
  check(!p && error.line == 5);
  check(pbqp_read(nullptr, &p, &error) == PBQP_INVALID && !p);
  check(pbqp_validate(nullptr, &error) == PBQP_INVALID);
}

void test_error_contract() {
  auto invoke = [](auto call, pbqp_status expected) {
    pbqp_error error;
    error.line = 123;
    std::fill_n(error.message, sizeof(error.message), 'x');
    check(call(&error) == expected);
    check(std::find(error.message, error.message + sizeof(error.message),
                    '\0') != error.message + sizeof(error.message));
    if (expected == PBQP_OK)
      check(error.line == 0 && error.message[0] == '\0');
    else
      check(error.message[0] != '\0');
  };
  pbqp_problem *parsed = nullptr;
  invoke(
      [&](pbqp_error *e) {
        return read_text("PBQP 1 NODES 0 EDGES 0 END", &parsed, e);
      },
      PBQP_OK);
  pbqp_problem_free(parsed);
  invoke([&](pbqp_error *e) { return read_text("PBQP 2", &parsed, e); },
         PBQP_INVALID);

  pbqp_cost_t cost = 1.0f;
  pbqp_node node{1, &cost};
  pbqp_problem problem{1, 0, &node, nullptr};
  invoke([&](pbqp_error *e) { return pbqp_validate(&problem, e); }, PBQP_OK);
  invoke([](pbqp_error *e) { return pbqp_validate(nullptr, e); }, PBQP_INVALID);
  TemporaryFile file;
  invoke(
      [&](pbqp_error *e) {
        return pbqp_write(file.name().c_str(), &problem, e);
      },
      PBQP_OK);
  invoke([&](pbqp_error *e) { return pbqp_write(nullptr, &problem, e); },
         PBQP_INVALID);

  pbqp_solution *solution = nullptr;
  invoke(
      [&](pbqp_error *e) {
        return pbqp_solve_bruteforce(&problem, &solution, e);
      },
      PBQP_OK);
  pbqp_solution_free(solution);
  pbqp_evaluation evaluation{};
  size_t option = 0;
  invoke(
      [&](pbqp_error *e) {
        return pbqp_evaluate_assignment(&problem, &option, 1, &evaluation, e);
      },
      PBQP_OK);
  invoke(
      [&](pbqp_error *e) {
        return pbqp_evaluate_assignment(&problem, nullptr, 1, &evaluation, e);
      },
      PBQP_INVALID);
  invoke(
      [&](pbqp_error *e) { return pbqp_solve_greedy(&problem, &solution, e); },
      PBQP_OK);
  pbqp_solution_free(solution);
  invoke(
      [&](pbqp_error *e) {
        return pbqp_solve_reduction(&problem, &solution, e);
      },
      PBQP_OK);
  pbqp_solution_free(solution);
  cost = PBQP_INF;
  invoke(
      [&](pbqp_error *e) {
        return pbqp_solve_bruteforce(&problem, &solution, e);
      },
      PBQP_INFEASIBLE);
  invoke(
      [&](pbqp_error *e) { return pbqp_solve_greedy(&problem, &solution, e); },
      PBQP_NO_SOLUTION);
  invoke(
      [&](pbqp_error *e) {
        return pbqp_solve_reduction(&problem, &solution, e);
      },
      PBQP_NO_SOLUTION);
  invoke(
      [&](pbqp_error *e) {
        return pbqp_evaluate_assignment(&problem, &option, 1, &evaluation, e);
      },
      PBQP_OK);
  check(!evaluation.is_feasible && evaluation.cost == PBQP_INF);
  check(!solution);
}

void test_float_validation() {
  pbqp_cost_t cost = std::numeric_limits<pbqp_cost_t>::quiet_NaN();
  pbqp_node node{1, &cost};
  pbqp_edge edge{0, 1, &cost};
  pbqp_problem p{1, 0, &node, nullptr};
  check(pbqp_validate(&p, nullptr) == PBQP_INVALID);
  cost = -PBQP_INF;
  check(pbqp_validate(&p, nullptr) == PBQP_INVALID);
  cost = PBQP_INF;
  check(pbqp_validate(&p, nullptr) == PBQP_OK);
  pbqp_cost_t zero = 0.0f;
  pbqp_node nodes[] = {{1, &zero}, {1, &zero}};
  p = {2, 1, nodes, &edge};
  cost = std::numeric_limits<pbqp_cost_t>::quiet_NaN();
  check(pbqp_validate(&p, nullptr) == PBQP_INVALID);
  cost = -PBQP_INF;
  check(pbqp_validate(&p, nullptr) == PBQP_INVALID);
  cost = PBQP_INF;
  check(pbqp_validate(&p, nullptr) == PBQP_OK);
  check_roundtrip(&p);
}

void check_reference(const pbqp_problem &p) {
  std::vector<size_t> tuple(p.num_nodes), best;
  pbqp_cost_t optimum = 0;
  bool found = false;
  // Independently evaluate full tuples in the public reference order.
  std::function<void(size_t)> visit = [&](size_t node) {
    if (node == p.num_nodes) {
      pbqp_cost_t total = 0;
      for (size_t k = 0; k < p.num_nodes; ++k) {
        auto unary = p.nodes[k].costs[tuple[k]];
        if (unary == PBQP_INF)
          return;
        total = total + unary;
      }
      for (size_t k = 0; k < p.num_edges; ++k) {
        const auto &e = p.edges[k];
        auto pair =
            e.costs[tuple[e.src] * p.nodes[e.tgt].num_options + tuple[e.tgt]];
        if (pair == PBQP_INF)
          return;
        total = total + pair;
      }
      if (!found || total < optimum) {
        found = true;
        optimum = total;
        best = tuple;
      }
      return;
    }
    for (size_t option = 0; option < p.nodes[node].num_options; ++option) {
      tuple[node] = option;
      visit(node + 1);
    }
  };
  visit(0);
  const auto free_solution = [](pbqp_solution *s) noexcept {
    pbqp_solution_free(s);
  };
  auto verify_exact = [&](auto solve, std::string_view name,
                          bool match_tuple = true) {
    pbqp_solution *raw = nullptr;
    auto status = solve(&p, &raw, nullptr);
    std::unique_ptr<pbqp_solution, decltype(free_solution)> solution(
        raw, free_solution);
    bool matches = found ? status == PBQP_OK && solution &&
                               solution->cost == optimum &&
                               solution->num_nodes == best.size()
                         : status == PBQP_INFEASIBLE && !solution;
    if (found && matches) {
      pbqp_evaluation evaluation{};
      matches =
          pbqp_evaluate_assignment(&p, solution->options, solution->num_nodes,
                                   &evaluation, nullptr) == PBQP_OK &&
          evaluation.is_feasible && evaluation.cost == optimum;
    }
    if (found && matches && match_tuple)
      for (size_t k = 0; k < p.num_nodes; ++k)
        matches &= solution->options[k] == best[k];
    if (!matches) {
      auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      auto path = std::filesystem::temp_directory_path() /
                  ("pbqp-mismatch-" + std::to_string(stamp) + ".pbqp");
      pbqp_error error{};
      auto written = pbqp_write(path.string().c_str(), &p, &error);
      std::cerr << "Case: " << case_context << "\nExpected feasible=" << found
                << " cost=" << optimum << " options:";
      for (auto option : best)
        std::cerr << ' ' << option;
      std::cerr << "\nActual solver=" << name << " status=" << status;
      if (solution) {
        std::cerr << " cost=" << solution->cost << " options:";
        for (size_t k = 0; k < solution->num_nodes; ++k)
          std::cerr << ' ' << solution->options[k];
      }
      std::cerr << "\nProblem dump: "
                << (written == PBQP_OK ? path.string() : error.message) << '\n';
    }
    check(matches);
    if (solution)
      check(solution->quality == PBQP_SOLUTION_OPTIMAL);
  };
  verify_exact(pbqp_solve_bruteforce, "naive");
  verify_exact(pbqp_solve_branch_and_bound, "bnb", false);
  for (size_t threads : {size_t{2}, size_t{4}}) {
    auto parallel = [&](const pbqp_problem *p, pbqp_solution **out,
                        pbqp_error *error) {
      const pbqp_solve_options options{threads, nullptr, nullptr};
      return pbqp_solve_branch_and_bound_with_options(p, &options, out, error);
    };
    verify_exact(parallel, "parallel bnb", false);
    auto exhaustive = [&](const pbqp_problem *p, pbqp_solution **out,
                          pbqp_error *error) {
      const pbqp_solve_options options{threads, nullptr, nullptr};
      return pbqp_solve_bruteforce_with_options(p, &options, out, error);
    };
    verify_exact(exhaustive, "parallel naive");
  }

  for (auto solve : {pbqp_solve_greedy, pbqp_solve_reduction}) {
    pbqp_solution *raw = nullptr;
    auto status = solve(&p, &raw, nullptr);
    std::unique_ptr<pbqp_solution, decltype(free_solution)> heuristic(
        raw, free_solution);
    if (status == PBQP_OK) {
      check(found && heuristic && heuristic->quality == PBQP_SOLUTION_FEASIBLE);
      pbqp_evaluation evaluation{};
      check(pbqp_evaluate_assignment(&p, heuristic->options,
                                     heuristic->num_nodes, &evaluation,
                                     nullptr) == PBQP_OK);
      check(evaluation.is_feasible && heuristic->cost == evaluation.cost &&
            heuristic->cost >= optimum);
    } else
      check(status == PBQP_NO_SOLUTION && !heuristic);
  }
}

void test_generator() {
  for (std::uint64_t seed = 0; seed < 200; ++seed) {
    size_t n = seed % 6;
    size_t pairs = n ? n * (n - 1) / 2 : 0;
    pbqp_tools::GeneratorOptions options{n, (seed / 6) % (pairs + 1), 3, seed};
    case_context = "seed=" + std::to_string(seed) +
                   " nodes=" + std::to_string(n) +
                   " edges=" + std::to_string(options.num_edges);
    const auto generated = pbqp_tools::generate_problem(options);
    auto other = pbqp_tools::generate_problem(options);
    auto moved = std::move(other);
    auto p = generated.view(), copy = moved.view();
    check(p.num_nodes == options.num_nodes && p.num_edges == options.num_edges);
    check(pbqp_validate(&p, nullptr) == PBQP_OK);
    same_problem(&p, &copy);
    for (size_t i = 0; i < p.num_nodes; ++i)
      check(p.nodes[i].num_options >= 1 &&
            p.nodes[i].num_options <= options.max_options);
    check_roundtrip(&p);
    check_reference(p);
    if (p.num_edges > 1) {
      std::vector<pbqp_edge> reversed(p.edges, p.edges + p.num_edges);
      std::reverse(reversed.begin(), reversed.end());
      p.edges = reversed.data();
      check_roundtrip(&p);
      check_reference(p);
    }
  }
  case_context.clear();
  for (const auto &options :
       {pbqp_tools::GeneratorOptions{1, 1, 2, 0}, {2, 2, 2, 0}, {1, 0, 0, 0}}) {
    bool rejected = false;
    try {
      auto ignored = pbqp_tools::generate_problem(options);
    } catch (const std::invalid_argument &) {
      rejected = true;
    }
    check(rejected);
  }
}

void test_direct_api() {
  pbqp_cost_t costs[] = {0, 1};
  pbqp_node nodes[] = {{2, costs}, {2, costs}};
  pbqp_cost_t matrix[] = {0, 0, 0, 0};
  pbqp_edge edges[] = {{0, 1, matrix}, {0, 1, matrix}};
  pbqp_problem p{2, 1, nodes, edges};
  check(pbqp_validate(&p, nullptr) == PBQP_OK);
  auto invalid = [&](const pbqp_problem &problem) {
    pbqp_error error{};
    check(pbqp_validate(&problem, &error) == PBQP_INVALID);
    check(error.message[0]);
    for (auto solve : {pbqp_solve_bruteforce, pbqp_solve_greedy,
                       pbqp_solve_branch_and_bound, pbqp_solve_reduction}) {
      pbqp_solution *out = nullptr;
      check(solve(&problem, &out, &error) == PBQP_INVALID && !out);
    }
  };
  p.nodes = nullptr;
  invalid(p);
  p.nodes = nodes;
  p.edges = nullptr;
  invalid(p);
  p.edges = edges;
  nodes[0].num_options = 0;
  invalid(p);
  nodes[0].num_options = 2;
  nodes[0].costs = nullptr;
  invalid(p);
  nodes[0].costs = costs;
  edges[0].costs = nullptr;
  invalid(p);
  edges[0].costs = matrix;
  edges[0].src = 1;
  invalid(p);
  edges[0].src = 0;
  edges[0].tgt = 2;
  invalid(p);
  edges[0].tgt = 1;
  p.num_edges = 2;
  invalid(p);
  p.num_edges = 1;
  for (auto solve : {pbqp_solve_bruteforce, pbqp_solve_greedy,
                     pbqp_solve_branch_and_bound, pbqp_solve_reduction})
    check(solve(&p, nullptr, nullptr) == PBQP_INVALID);
  check(pbqp_read("unused", nullptr, nullptr) == PBQP_INVALID);
  pbqp_problem empty{0, 0, nodes, edges};
  check(pbqp_validate(&empty, nullptr) == PBQP_OK);
  size_t assignment[] = {0, 1, 0};
  pbqp_evaluation evaluation{99, 1};
  check(pbqp_evaluate_assignment(&p, assignment, 2, &evaluation, nullptr) ==
        PBQP_OK);
  check(evaluation.is_feasible && evaluation.cost == 1);
  check(pbqp_evaluate_assignment(&p, assignment, 2, nullptr, nullptr) ==
        PBQP_INVALID);
  for (size_t length : {size_t{0}, size_t{1}, size_t{3}}) {
    check(pbqp_evaluate_assignment(&p, assignment, length, &evaluation,
                                   nullptr) == PBQP_INVALID);
    check(evaluation.cost == 0 && !evaluation.is_feasible);
  }
  check(pbqp_evaluate_assignment(&p, nullptr, 2, &evaluation, nullptr) ==
        PBQP_INVALID);
  assignment[1] = 2;
  check(pbqp_evaluate_assignment(&p, assignment, 2, &evaluation, nullptr) ==
        PBQP_INVALID);
  assignment[1] = 1;
  matrix[1] = PBQP_INF;
  check(pbqp_evaluate_assignment(&p, assignment, 2, &evaluation, nullptr) ==
        PBQP_OK);
  check(!evaluation.is_feasible && evaluation.cost == PBQP_INF);
  matrix[1] = 0;
  costs[0] = costs[1] = std::numeric_limits<pbqp_cost_t>::max();
  check(pbqp_evaluate_assignment(&p, assignment, 2, &evaluation, nullptr) ==
        PBQP_OK);
  check(evaluation.is_feasible && evaluation.cost == PBQP_INF);
  check_reference(p);
  check(pbqp_evaluate_assignment(&empty, nullptr, 0, &evaluation, nullptr) ==
        PBQP_OK);
  check(evaluation.is_feasible && evaluation.cost == 0);

  pbqp_problem *parsed = nullptr;
  check(read_text("PBQP 1 NODES 1 EDGES 0 NODE 0 2 2 -3 END", &parsed,
                  nullptr) == PBQP_OK);
  pbqp_solution *naive = nullptr, *greedy = nullptr, *bounded = nullptr,
                *reduced = nullptr;
  check(pbqp_solve_bruteforce(parsed, &naive, nullptr) == PBQP_OK);
  check(pbqp_solve_greedy(parsed, &greedy, nullptr) == PBQP_OK);
  check(pbqp_solve_branch_and_bound(parsed, &bounded, nullptr) == PBQP_OK);
  check(pbqp_solve_reduction(parsed, &reduced, nullptr) == PBQP_OK);
  pbqp_problem_free(parsed);
  for (auto solution : {naive, greedy, bounded, reduced}) {
    check(solution->num_nodes == 1 && solution->options[0] == 1 &&
          solution->cost == -3);
    pbqp_solution_free(solution);
  }
}

void test_greedy_and_rounding() {
  pbqp_problem *p = nullptr;
  // The cheap first option is a trap. Only backtracking discovers feasibility.
  check(read_text(
            "PBQP 1 NODES 2 EDGES 1 NODE 0 2 0 1 NODE 1 1 0 EDGE 0 1 INF 0 END",
            &p, nullptr) == PBQP_OK);
  pbqp_solution *s = nullptr;
  pbqp_error error{};
  check(pbqp_solve_greedy(p, &s, &error) == PBQP_NO_SOLUTION && !s &&
        error.message[0]);
  check(pbqp_solve_bruteforce(p, &s, nullptr) == PBQP_OK && s->cost == 1);
  pbqp_solution_free(s);
  pbqp_problem_free(p);
  check(read_text("PBQP 1 NODES 0 EDGES 0 END", &p, nullptr) == PBQP_OK);
  check(pbqp_solve_greedy(p, &s, nullptr) == PBQP_OK && s->cost == 0 &&
        s->quality == PBQP_SOLUTION_FEASIBLE);
  pbqp_solution_free(s);
  pbqp_problem_free(p);
  // A higher-priority target is assigned before its source. The rectangular
  // matrix must still be indexed by source rows and target columns.
  check(read_text("PBQP 1 NODES 2 EDGES 1 NODE 0 2 -100 -99 NODE 1 3 2 10 INF "
                  "EDGE 0 1 INF 0 0 0 0 0 END",
                  &p, nullptr) == PBQP_OK);
  check(pbqp_solve_greedy(p, &s, nullptr) == PBQP_OK);
  check(s->cost == -97 && s->options[0] == 1 && s->options[1] == 0);
  pbqp_evaluation evaluation{};
  check(pbqp_evaluate_assignment(p, s->options, s->num_nodes, &evaluation,
                                 nullptr) == PBQP_OK);
  check(evaluation.is_feasible && evaluation.cost == s->cost);
  pbqp_solution_free(s);
  pbqp_problem_free(p);
  const size_t first[] = {0, 0, 0};
  solve_text("PBQP 1 NODES 3 EDGES 0 NODE 0 1 16777216 NODE 1 2 1 0 NODE 2 1 "
             "-16777216 END",
             PBQP_OK, 0, first);
  check(read_text("PBQP 1 NODES 3 EDGES 0 NODE 0 1 16777216 NODE 1 2 1 0 NODE "
                  "2 1 -16777216 END",
                  &p, nullptr) == PBQP_OK);
  check_reference(*p);
  pbqp_problem_free(p);
  solve_text("PBQP 1 NODES 1 EDGES 0 NODE 0 3 +1 -1 +1e2 END", PBQP_OK, -1,
             nullptr);
}

void test_io_failures() {
  pbqp_error error{};
  pbqp_problem *p = nullptr;
  TemporaryFile missing;
  check(pbqp_read(missing.name().c_str(), &p, &error) == PBQP_IO_ERROR && !p &&
        error.message[0]);
  pbqp_problem empty{};
  auto bad_destination = missing.name() + "/problem.pbqp";
  check(pbqp_write(bad_destination.c_str(), &empty, &error) == PBQP_IO_ERROR &&
        error.message[0]);
  if (std::filesystem::exists("/dev/full"))
    check(pbqp_write("/dev/full", &empty, &error) == PBQP_IO_ERROR &&
          error.message[0]);
  std::string text = "PBQP 1 NODES 2 EDGES 1 NODE 0 2 -1 0 NODE 1 2 2 3 EDGE 0 "
                     "1 0 INF 4 5 END";
  for (size_t cut = 0; cut < text.size(); ++cut) {
    case_context = "truncated input length=" + std::to_string(cut);
    check(read_text(text.substr(0, cut), &p, &error) == PBQP_INVALID && !p &&
          error.message[0]);
  }
  case_context.clear();
  TemporaryFile existing;
  {
    std::ofstream out(existing.name());
    out << "preserve";
  }
  pbqp_problem bad{1, 0, nullptr, nullptr};
  check(pbqp_write(existing.name().c_str(), &bad, &error) == PBQP_INVALID);
  std::ifstream in(existing.name());
  std::string contents;
  in >> contents;
  check(contents == "preserve");
}

void test_reduction() {
  const auto free_problem = [](pbqp_problem *p) noexcept {
    pbqp_problem_free(p);
  };
  const auto free_solution = [](pbqp_solution *s) noexcept {
    pbqp_solution_free(s);
  };
  auto verify = [&](const std::string &text, pbqp_status expected) {
    pbqp_problem *raw = nullptr, *copy = nullptr;
    check(read_text(text, &raw, nullptr) == PBQP_OK);
    std::unique_ptr<pbqp_problem, decltype(free_problem)> problem(raw,
                                                                  free_problem);
    check(read_text(text, &copy, nullptr) == PBQP_OK);
    std::unique_ptr<pbqp_problem, decltype(free_problem)> original(
        copy, free_problem);
    pbqp_solution *answer = nullptr;
    pbqp_error error{42, "old diagnostic"};
    check(pbqp_solve_reduction(problem.get(), &answer, &error) == expected);
    std::unique_ptr<pbqp_solution, decltype(free_solution)> solution(
        answer, free_solution);
    same_problem(problem.get(), original.get());
    if (expected != PBQP_OK) {
      check(!solution && error.message[0] && error.line == 0);
      return;
    }
    check(solution->quality == PBQP_SOLUTION_FEASIBLE && !error.line &&
          !error.message[0]);
    pbqp_evaluation evaluation{};
    check(pbqp_evaluate_assignment(problem.get(), solution->options,
                                   solution->num_nodes, &evaluation,
                                   nullptr) == PBQP_OK);
    check(evaluation.is_feasible && evaluation.cost == solution->cost);
    // These integer/quarter-step fixtures need only R0/R1/R2, whose local
    // regrouping is exact here. Their costs must match exhaustive search.
    check(pbqp_solve_bruteforce(problem.get(), &answer, nullptr) == PBQP_OK);
    std::unique_ptr<pbqp_solution, decltype(free_solution)> exact(
        answer, free_solution);
    check(solution->cost == exact->cost);
  };
  verify("PBQP 1 NODES 0 EDGES 0 END", PBQP_OK);
  verify("PBQP 1 NODES 1 EDGES 0 NODE 0 3 INF -2 -2 END", PBQP_OK);
  verify("PBQP 1 NODES 1 EDGES 0 NODE 0 1 INF END", PBQP_NO_SOLUTION);
  // R1 avoids the forward greedy trap and respects rectangular edge
  // orientation.
  verify("PBQP 1 NODES 2 EDGES 1 NODE 0 2 0 1 NODE 1 1 0 EDGE 0 1 INF 0 END",
         PBQP_OK);
  verify("PBQP 1 NODES 2 EDGES 1 NODE 0 2 -100 -99 NODE 1 3 2 10 INF "
         "EDGE 0 1 INF 0 0 0 0 0 END",
         PBQP_OK);
  // The leaves are targets; node 0 survives until their unary deltas arrive.
  verify("PBQP 1 NODES 4 EDGES 3 NODE 0 2 0 1 NODE 1 1 0 NODE 2 2 1 -1 "
         "NODE 3 1 -2 EDGE 0 1 3 0 EDGE 0 2 INF 0 0 2 EDGE 0 3 1 0 END",
         PBQP_OK);
  // A triangle merges R2's delta into an existing neighbor edge.
  verify("PBQP 1 NODES 3 EDGES 3 NODE 0 2 0 1 NODE 1 3 2 0 -1 NODE 2 2 1 0 "
         "EDGE 0 1 0 2 INF -3 1 0 EDGE 0 2 2 0 1 INF EDGE 1 2 1 0 -1 2 0 3 END",
         PBQP_OK);
  // A four-cycle creates a fill edge, then merges into an existing edge.
  verify("PBQP 1 NODES 4 EDGES 4 NODE 0 2 -1 2 NODE 1 3 0 1 2 NODE 2 2 2 -2 "
         "NODE 3 1 1 EDGE 0 1 0 1 INF 2 0 -1 EDGE 0 3 0 2 "
         "EDGE 1 2 1 0 -1 2 0 3 EDGE 2 3 4 0 END",
         PBQP_OK);
  // Singleton domains and disconnected components remain ordinary reductions.
  verify("PBQP 1 NODES 4 EDGES 2 NODE 0 1 -2 NODE 1 2 0 -1 NODE 2 1 3 "
         "NODE 3 1 -4 EDGE 0 1 0 INF EDGE 2 3 -1 END",
         PBQP_OK);
  // Infinite accumulated values are feasible, even after R1 and R2 propagation.
  verify("PBQP 1 NODES 2 EDGES 1 NODE 0 1 3e38 NODE 1 1 3e38 EDGE 0 1 0 END",
         PBQP_OK);
  verify("PBQP 1 NODES 3 EDGES 3 NODE 0 1 -3e38 NODE 1 1 -3e38 NODE 2 1 3e38 "
         "EDGE 0 1 0 EDGE 0 2 0 EDGE 1 2 0 END",
         PBQP_OK);

  // RN removes cheap node 0. Its neighbors independently choose zero, trapping
  // reverse assignment although an exact solver can find a feasible tuple.
  pbqp_problem *raw = nullptr;
  check(read_text("PBQP 1 NODES 4 EDGES 6 NODE 0 2 0 0 NODE 1 2 1 1 "
                  "NODE 2 2 1 1 NODE 3 2 1 1 EDGE 0 1 INF 0 0 INF "
                  "EDGE 0 2 0 INF INF 0 EDGE 0 3 0 INF INF 0 "
                  "EDGE 1 2 0 0 0 0 EDGE 1 3 0 0 0 0 EDGE 2 3 0 0 0 0 END",
                  &raw, nullptr) == PBQP_OK);
  std::unique_ptr<pbqp_problem, decltype(free_problem)> trap(raw, free_problem);
  pbqp_solution *answer = nullptr;
  check(pbqp_solve_reduction(trap.get(), &answer, nullptr) ==
            PBQP_NO_SOLUTION &&
        !answer);
  check(pbqp_solve_bruteforce(trap.get(), &answer, nullptr) == PBQP_OK);
  pbqp_solution_free(answer);

  check(pbqp_read("examples/06_too_long_for_any_exact.pbqp", &raw, nullptr) ==
        PBQP_OK);
  std::unique_ptr<pbqp_problem, decltype(free_problem)> large(raw,
                                                              free_problem);
  pbqp_cost_t greedy_cost = 0;
  for (auto solve : {pbqp_solve_greedy, pbqp_solve_reduction}) {
    check(solve(large.get(), &answer, nullptr) == PBQP_OK);
    std::unique_ptr<pbqp_solution, decltype(free_solution)> solution(
        answer, free_solution);
    check(solution->num_nodes == 20 &&
          solution->quality == PBQP_SOLUTION_FEASIBLE);
    pbqp_evaluation evaluation{};
    check(pbqp_evaluate_assignment(large.get(), solution->options,
                                   solution->num_nodes, &evaluation,
                                   nullptr) == PBQP_OK);
    check(evaluation.is_feasible && evaluation.cost == solution->cost);
    if (solve == pbqp_solve_greedy)
      greedy_cost = solution->cost;
    else
      check(solution->cost < greedy_cost);
  }
}

void test_branch_and_bound_seed() {
  const auto free_problem = [](pbqp_problem *p) noexcept {
    pbqp_problem_free(p);
  };
  const auto free_solution = [](pbqp_solution *s) noexcept {
    pbqp_solution_free(s);
  };
  auto verify = [&](const std::string &text, pbqp_status greedy_status,
                    pbqp_cost_t greedy_cost,
                    const std::vector<size_t> &greedy_options) {
    pbqp_problem *raw_problem = nullptr;
    check(read_text(text, &raw_problem, nullptr) == PBQP_OK);
    std::unique_ptr<pbqp_problem, decltype(free_problem)> problem(raw_problem,
                                                                  free_problem);
    pbqp_solution *raw_solution = nullptr;
    check(pbqp_solve_greedy(problem.get(), &raw_solution, nullptr) ==
          greedy_status);
    std::unique_ptr<pbqp_solution, decltype(free_solution)> greedy(
        raw_solution, free_solution);
    if (greedy_status == PBQP_OK) {
      check(greedy->cost == greedy_cost &&
            greedy->num_nodes == greedy_options.size());
      check(std::equal(greedy_options.begin(), greedy_options.end(),
                       greedy->options));
    } else {
      check(!greedy);
    }
    // Both exact solvers must match the independent optimum cost.
    check_reference(*problem);
    pbqp_error error{42, "old greedy diagnostic"};
    check(pbqp_solve_branch_and_bound(problem.get(), &raw_solution, &error) ==
          PBQP_OK);
    std::unique_ptr<pbqp_solution, decltype(free_solution)> exact(
        raw_solution, free_solution);
    check(exact->quality == PBQP_SOLUTION_OPTIMAL && !error.line &&
          !error.message[0]);
  };
  // Greedy gets trapped; B&B must continue and find the only feasible tuple.
  verify("PBQP 1 NODES 2 EDGES 1 NODE 0 2 0 1 NODE 1 1 0 EDGE 0 1 INF 0 END",
         PBQP_NO_SOLUTION, 0, {});
  // Greedy's initial upper bound is feasible but requires exact improvement.
  verify("PBQP 1 NODES 2 EDGES 1 NODE 0 2 0 1 NODE 1 2 0 0 "
         "EDGE 0 1 10 10 0 0 END",
         PBQP_OK, 10, {0, 0});
  // Reference rounding makes greedy's later tuple tie the first optimum.
  verify("PBQP 1 NODES 3 EDGES 0 NODE 0 1 16777216 NODE 1 2 1 0 "
         "NODE 2 1 -16777216 END",
         PBQP_OK, 0, {0, 1, 0});
  // An infinite seed is feasible; it must not hide an earlier equal tuple.
  verify("PBQP 1 NODES 2 EDGES 0 NODE 0 2 3e38 2e38 NODE 1 1 3e38 END", PBQP_OK,
         PBQP_INF, {1, 0});
  verify("PBQP 1 NODES 2 EDGES 0 NODE 0 2 -2e38 -3e38 NODE 1 1 -3e38 END",
         PBQP_OK, -PBQP_INF, {1, 0});
}

void test_branch_and_bound_bounds() {
  const size_t choice[] = {1, 0};
  // A positive assigned unary cost cannot justify pruning a negative edge.
  solve_text("PBQP 1 NODES 2 EDGES 1 NODE 0 2 0 10 NODE 1 1 0 "
             "EDGE 0 1 0 -100 END",
             PBQP_OK, -90, choice);
  // Regrouping the two remaining unary costs would produce an unsafe bound
  // of 1 instead of 0 for the better branch, after finding incumbent 0.5.
  const size_t rounded_choice[] = {1, 0, 0};
  solve_text("PBQP 1 NODES 3 EDGES 1 NODE 0 2 16777216 16777216 "
             "NODE 1 1 1 NODE 2 1 -16777216 EDGE 0 2 0.5 0 END",
             PBQP_OK, 0, rounded_choice);
  const auto maximum = std::numeric_limits<pbqp_cost_t>::max();
  const std::vector<pbqp_cost_t> pool{
      0,         0.5f,
      -0.5f,     1,
      -1,        16777216,
      -16777216, maximum,
      -maximum,  std::numeric_limits<pbqp_cost_t>::denorm_min(),
      PBQP_INF};
  for (unsigned seed = 0; seed < 250; ++seed) {
    case_context = "float-bound seed=" + std::to_string(seed);
    std::mt19937 random(seed);
    std::vector<std::vector<pbqp_cost_t>> unary(4, std::vector<pbqp_cost_t>(2));
    std::vector<std::vector<pbqp_cost_t>> pairwise(6,
                                                   std::vector<pbqp_cost_t>(4));
    std::vector<pbqp_node> nodes;
    std::vector<pbqp_edge> edges;
    for (auto &costs : unary) {
      for (auto &cost : costs)
        cost = pool[random() % pool.size()];
      nodes.push_back({2, costs.data()});
    }
    for (size_t src = 0; src < 4; ++src)
      for (size_t tgt = src + 1; tgt < 4; ++tgt) {
        auto &costs = pairwise[edges.size()];
        for (auto &cost : costs)
          cost = pool[random() % pool.size()];
        edges.push_back({src, tgt, costs.data()});
      }
    if (seed % 2)
      std::reverse(edges.begin(), edges.end());
    pbqp_problem p{nodes.size(), edges.size(), nodes.data(), edges.data()};
    check_reference(p);
  }
  case_context.clear();
}

void test_generator_families() {
  using pbqp_tools::GeneratorFamily;
  for (auto family : {GeneratorFamily::Planted, GeneratorFamily::RegAlloc}) {
    for (std::uint64_t seed = 0; seed < 80; ++seed) {
      size_t n = seed % 6;
      size_t pairs = n ? n * (n - 1) / 2 : 0;
      pbqp_tools::GeneratorOptions options{
          n, seed % (pairs + 1), 3, seed, family, 2, 10, 20};
      case_context = "family=" + std::to_string(static_cast<int>(family)) +
                     " seed=" + std::to_string(seed) +
                     " nodes=" + std::to_string(n) +
                     " edges=" + std::to_string(options.num_edges);
      const auto generated = pbqp_tools::generate_problem(options);
      const auto second = pbqp_tools::generate_problem(options);
      auto p = generated.view(), copy = second.view();
      same_problem(&p, &copy);
      check_roundtrip(&p);
      check_reference(p);
      pbqp_solution *solution = nullptr;
      check(pbqp_solve_bruteforce(&p, &solution, nullptr) == PBQP_OK);
      pbqp_solution_free(solution);
      if (family == GeneratorFamily::RegAlloc) {
        for (size_t i = 0; i < p.num_nodes; ++i) {
          check(p.nodes[i].num_options == 3);
          check(p.nodes[i].costs[0] >= 10 && p.nodes[i].costs[0] <= 20);
          check(p.nodes[i].costs[1] == 0 && p.nodes[i].costs[2] == 0);
        }
        for (size_t k = 0; k < p.num_edges; ++k)
          for (size_t row = 0; row < 3; ++row)
            for (size_t col = 0; col < 3; ++col)
              check(p.edges[k].costs[row * 3 + col] ==
                    (row && row == col ? PBQP_INF : 0.0f));
        std::vector<size_t> spills(p.num_nodes, 0);
        pbqp_evaluation evaluation{};
        check(pbqp_evaluate_assignment(&p, spills.data(), spills.size(),
                                       &evaluation, nullptr) == PBQP_OK &&
              evaluation.is_feasible);
        check(pbqp_solve_greedy(&p, &solution, nullptr) == PBQP_OK);
        pbqp_solution_free(solution);
      }
    }
  }
  case_context.clear();
  // Sparse output must not require walking the 1.25-billion-pair universe.
  const auto sparse = pbqp_tools::generate_problem({50000, 1, 1, 42});
  auto p = sparse.view();
  check(p.num_nodes == 50000 && p.num_edges == 1 &&
        pbqp_validate(&p, nullptr) == PBQP_OK);
  const auto wide = pbqp_tools::generate_problem(
      {3, 3, 0, 42, GeneratorFamily::RegAlloc, 32, 25, 25});
  auto wide_problem = wide.view();
  check(wide_problem.nodes[0].num_options == 33);
  pbqp_solution *wide_solution = nullptr;
  check(pbqp_solve_greedy(&wide_problem, &wide_solution, nullptr) == PBQP_OK &&
        wide_solution->cost == 0);
  pbqp_solution_free(wide_solution);
  for (size_t edges : {size_t{0}, size_t{1}, size_t{14}, size_t{15}}) {
    const auto generated = pbqp_tools::generate_problem({6, edges, 1, 42});
    auto graph = generated.view();
    check(graph.num_edges == edges &&
          pbqp_validate(&graph, nullptr) == PBQP_OK);
    for (size_t k = 1; k < edges; ++k)
      check(graph.edges[k - 1].src < graph.edges[k].src ||
            (graph.edges[k - 1].src == graph.edges[k].src &&
             graph.edges[k - 1].tgt < graph.edges[k].tgt));
  }
}

struct ProgressLog {
  unsigned count = 0;
  unsigned percentages[100]{};
};

void collect_progress(unsigned percent, void *data) noexcept {
  auto &log = *static_cast<ProgressLog *>(data);
  if (log.count < 100)
    log.percentages[log.count] = percent;
  ++log.count;
}

void test_progress() {
  for (const auto &dimensions : {std::vector<size_t>{},
                                 {1},
                                 {2},
                                 {7},
                                 {100},
                                 {2, 5, 10},
                                 {4, 5, 7},
                                 {1, 3, 1, 7}}) {
    pbqp_problem p{dimensions.size(), 0, nullptr, nullptr};
    std::vector<std::vector<pbqp_cost_t>> costs;
    std::vector<pbqp_node> nodes;
    costs.reserve(dimensions.size());
    nodes.reserve(dimensions.size());
    for (auto d : dimensions) {
      costs.emplace_back(d, 1.0f);
      nodes.push_back({d, costs.back().data()});
    }
    p.nodes = nodes.data();
    for (bool forbidden : {false, true}) {
      // Make the complete space forbidden while still reporting all work.
      if (forbidden && !costs.empty())
        std::fill(costs.back().begin(), costs.back().end(), PBQP_INF);
      for (auto solve : {pbqp_solve_bruteforce_with_progress,
                         pbqp_solve_branch_and_bound_with_progress}) {
        ProgressLog log;
        pbqp_solution *progress = nullptr, *plain = nullptr;
        auto status = solve(&p, collect_progress, &log, &progress, nullptr);
        check(status == pbqp_solve_bruteforce(&p, &plain, nullptr));
        check(log.count == 100);
        for (unsigned i = 0; i < 100; ++i)
          check(log.percentages[i] == i + 1);
        if (plain) {
          check(progress && progress->cost == plain->cost &&
                progress->quality == plain->quality);
          pbqp_evaluation evaluation{};
          check(pbqp_evaluate_assignment(&p, progress->options,
                                         progress->num_nodes, &evaluation,
                                         nullptr) == PBQP_OK);
          check(evaluation.is_feasible && evaluation.cost == plain->cost);
        } else
          check(!progress && status == PBQP_INFEASIBLE);
        pbqp_solution_free(plain);
        pbqp_solution_free(progress);
      }
    }
  }
  for (auto solve : {pbqp_solve_bruteforce_with_progress,
                     pbqp_solve_branch_and_bound_with_progress}) {
    ProgressLog log;
    pbqp_solution *solution = nullptr;
    check(solve(nullptr, collect_progress, &log, &solution, nullptr) ==
          PBQP_INVALID);
    check(!solution && log.count == 0);
    pbqp_problem empty{};
    check(solve(&empty, collect_progress, &log, nullptr, nullptr) ==
          PBQP_INVALID);
    check(log.count == 0);
    check(solve(&empty, nullptr, nullptr, &solution, nullptr) == PBQP_OK);
    pbqp_solution_free(solution);
  }
}
} // namespace

int main() {
  try {
    test_algebra();
    test_examples();
    test_solver();
    test_invalid();
    test_error_contract();
    test_float_validation();
    test_generator();
    test_direct_api();
    test_greedy_and_rounding();
    test_io_failures();
    test_generator_families();
    test_progress();
    test_reduction();
    test_branch_and_bound_seed();
    test_branch_and_bound_bounds();
    std::cout << "All PBQP tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
