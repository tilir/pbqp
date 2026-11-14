/*
 * Random problem generator command-line entry point.
 *
 * Parses named generation parameters and writes a PBQP 1 file through the C
 * API. A seed makes generation reproducible with the same standard library.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
#include "pbqp.h"
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import pbqp.command_line;
import pbqp.generator;

namespace {
template <typename T> T number(std::string_view name, std::string_view value) {
  T result{};
  auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
    throw std::invalid_argument(
        std::string(name) + ": expected an unsigned decimal integer in range");
  return result;
}

pbqp_cost_t spill_cost(std::string_view name, std::string_view value) {
  pbqp_cost_t result{};
  auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
      !std::isfinite(result))
    throw std::invalid_argument(std::string(name) +
                                ": expected a finite decimal cost");
  return result;
}

void help() {
  std::cout
      << "Usage: pbqp_generate --nodes=N --edges=M --seed=S --output=FILE "
         "[options]\n"
         "       pbqp_generate --help\n\n"
         "Required: --nodes=N --edges=M --seed=S --output=FILE\n"
         "Modes (select with --mode=NAME):\n"
         "  random   Unrestricted random costs (default); requires "
         "--max-options=D.\n"
         "  planted  Random costs with a feasible assignment; requires "
         "--max-options=D.\n"
         "  regalloc Register conflicts and expensive spill; requires "
         "--registers=R.\n\n"
         "  --max-options=D     Maximum options per node, positive.\n"
         "  --registers=R       Registers per node, positive; spill is option "
         "0.\n"
         "  --spill-cost-min=C  Minimum positive spill cost (default 10).\n"
         "  --spill-cost-max=C  Maximum spill cost (default 100).\n\n"
         "Regalloc edges have INF on the register diagonal and zero "
         "elsewhere.\n"
         "Spill bounds apply only to regalloc. All options also accept "
         "space-separated values.\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    constexpr std::string_view names[] = {
        "--nodes",     "--edges",          "--max-options",
        "--seed",      "--output",         "--mode",
        "--registers", "--spill-cost-min", "--spill-cost-max"};
    const pbqp_tools::Arguments arguments(argc, argv, names);
    if (arguments.help()) {
      help();
      return 0;
    }
    if (!arguments.positional().empty())
      throw std::invalid_argument(
          "use named options for all generation parameters");
    pbqp_tools::GeneratorOptions options{
        number<size_t>("--nodes", arguments.require("--nodes")),
        number<size_t>("--edges", arguments.require("--edges")), 0,
        number<std::uint64_t>("--seed", arguments.require("--seed"))};
    auto mode = arguments.get("--mode", "random");
    if (mode == "regalloc") {
      options.family = pbqp_tools::GeneratorFamily::RegAlloc;
      options.num_registers =
          number<size_t>("--registers", arguments.require("--registers"));
      options.spill_cost_min = spill_cost(
          "--spill-cost-min", arguments.get("--spill-cost-min", "10"));
      options.spill_cost_max = spill_cost(
          "--spill-cost-max", arguments.get("--spill-cost-max", "100"));
      if (arguments.has("--max-options"))
        throw std::invalid_argument("--max-options does not apply to regalloc");
    } else if (mode == "random" || mode == "planted") {
      options.family = mode == "random" ? pbqp_tools::GeneratorFamily::Random
                                        : pbqp_tools::GeneratorFamily::Planted;
      options.max_options =
          number<size_t>("--max-options", arguments.require("--max-options"));
      if (arguments.has("--registers") || arguments.has("--spill-cost-min") ||
          arguments.has("--spill-cost-max"))
        throw std::invalid_argument(
            "register and spill options require --mode=regalloc");
    } else
      throw std::invalid_argument("unknown generator mode: " +
                                  std::string(mode));
    auto output = arguments.require("--output");
    const auto generated = pbqp_tools::generate_problem(options);
    auto problem = generated.view();
    pbqp_error error{};
    if (pbqp_write(output.data(), &problem, &error) != PBQP_OK) {
      std::cerr << error.message << '\n';
      return 1;
    }
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\nUse --help for usage.\n";
    return 1;
  }
  return 0;
}
