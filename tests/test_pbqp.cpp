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
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

import pbqp.generator;

static void check(bool condition) {
  if (!condition)
    throw std::runtime_error("PBQP test failed");
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

static pbqp_status read_text(const std::string &text, pbqp_problem **p,
                             pbqp_error *error) {
  TemporaryFile file;
  std::ofstream output(file.name(), std::ios::binary);
  output << text;
  output.close();
  check(static_cast<bool>(output));
  return pbqp_read(file.name().c_str(), p, error);
}

static void same_problem(const pbqp_problem *a, const pbqp_problem *b) {
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

static void check_roundtrip(const pbqp_problem *p) {
  TemporaryFile file;
  pbqp_problem *copy = nullptr;
  pbqp_error error;
  check(pbqp_write(file.name().c_str(), p, &error) == PBQP_OK);
  check(pbqp_read(file.name().c_str(), &copy, &error) == PBQP_OK);
  same_problem(p, copy);
  pbqp_problem_free(copy);
}

static void test_examples() {
  const pbqp_cost_t expected[] = {0, 40, 80};
  for (size_t k = 0; k < 3; ++k) {
    pbqp_problem *p = nullptr;
    pbqp_solution *s = nullptr;
    pbqp_error error;
    auto path = "examples/" + std::to_string(k + 1) + ".pbqp";
    check(pbqp_read(path.c_str(), &p, &error) == PBQP_OK);
    check_roundtrip(p);
    check(pbqp_solve_bruteforce(p, &s, &error) == PBQP_OK);
    check(s->cost == expected[k]);
    check(s->quality == PBQP_SOLUTION_OPTIMAL);
    pbqp_solution_free(s);
    pbqp_problem_free(p);
  }
}

static void test_algebra() {
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

static void solve_text(const std::string &text, pbqp_status expected,
                       pbqp_cost_t cost, const size_t *options) {
  pbqp_problem *p = nullptr;
  pbqp_solution *s = nullptr;
  size_t k;
  check(read_text(text, &p, nullptr) == PBQP_OK);
  check_roundtrip(p);
  check(pbqp_solve_bruteforce(p, &s, nullptr) == expected);
  if (expected == PBQP_OK) {
    check(s && s->cost == cost && s->num_nodes == p->num_nodes);
    check(s->quality == PBQP_SOLUTION_OPTIMAL);
    for (k = 0; options && k < s->num_nodes; ++k)
      check(s->options[k] == options[k]);
  } else
    check(!s);
  pbqp_solution_free(s);
  pbqp_problem_free(p);
}

static void test_solver() {
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

static void test_invalid() {
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
  check(pbqp_read(nullptr, &p, &error) == PBQP_INVALID && !p);
  check(pbqp_validate(nullptr, &error) == PBQP_INVALID);
}

static void test_error_contract() {
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
  cost = PBQP_INF;
  invoke(
      [&](pbqp_error *e) {
        return pbqp_solve_bruteforce(&problem, &solution, e);
      },
      PBQP_INFEASIBLE);
  check(!solution);
}

static void test_float_validation() {
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

static void check_reference(const pbqp_problem &p) {
  std::vector<size_t> tuple(p.num_nodes), best;
  pbqp_cost_t optimum = PBQP_INF;
  std::function<void(size_t, pbqp_cost_t)> visit = [&](size_t node,
                                                       pbqp_cost_t partial) {
    if (node == p.num_nodes) {
      if (partial < optimum) {
        optimum = partial;
        best = tuple;
      }
      return;
    }
    for (size_t option = 0; option < p.nodes[node].num_options; ++option) {
      pbqp_cost_t unary = p.nodes[node].costs[option];
      if (unary == PBQP_INF)
        continue;
      pbqp_cost_t total = partial + unary;
      bool feasible = true;
      for (size_t k = 0; k < p.num_edges; ++k) {
        const auto &e = p.edges[k];
        if (e.tgt != node)
          continue;
        pbqp_cost_t pair =
            e.costs[tuple[e.src] * p.nodes[node].num_options + option];
        if (pair == PBQP_INF) {
          feasible = false;
          break;
        }
        total += pair;
      }
      if (!feasible)
        continue;
      tuple[node] = option;
      visit(node + 1, total);
    }
  };
  visit(0, 0);
  pbqp_solution *raw = nullptr;
  auto status = pbqp_solve_bruteforce(&p, &raw, nullptr);
  const auto free_solution = [](pbqp_solution *s) noexcept {
    pbqp_solution_free(s);
  };
  std::unique_ptr<pbqp_solution, decltype(free_solution)> solution(
      raw, free_solution);
  if (optimum == PBQP_INF)
    check(status == PBQP_INFEASIBLE && !solution);
  else {
    check(status == PBQP_OK && solution && solution->cost == optimum);
    check(solution->quality == PBQP_SOLUTION_OPTIMAL);
    for (size_t k = 0; k < p.num_nodes; ++k)
      check(solution->options[k] == best[k]);
  }
}

static void test_generator() {
  for (std::uint64_t seed = 0; seed < 200; ++seed) {
    size_t n = seed % 6;
    size_t pairs = n ? n * (n - 1) / 2 : 0;
    pbqp_tools::GeneratorOptions options{n, (seed / 6) % (pairs + 1), 3, seed};
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

int main() {
  try {
    test_algebra();
    test_examples();
    test_solver();
    test_invalid();
    test_error_contract();
    test_float_validation();
    test_generator();
    std::cout << "All PBQP tests passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  return 0;
}
