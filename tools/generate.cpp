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
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

import pbqp.command_line;
import pbqp.generator;

namespace {
template <class T> T number(std::string_view name, std::string_view value) {
  T result{};
  auto parsed =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
    throw std::invalid_argument(
        std::string(name) + ": expected an unsigned decimal integer in range");
  return result;
}

void help() {
  std::cout << "Usage: pbqp_generate --nodes=N --edges=M --max-options=D "
               "--seed=S --output=FILE\n"
               "       pbqp_generate --help\n\n"
               "All generation options are required:\n"
               "  --nodes=N        Number of nodes (zero is allowed).\n"
               "  --edges=M        Number of distinct undirected edges.\n"
               "  --max-options=D  Maximum options per node (positive).\n"
               "  --seed=S         Unsigned random seed.\n"
               "  --output=FILE    Destination PBQP file.\n\n"
               "Options also accept a space before the value.\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    constexpr std::string_view names[] = {"--nodes", "--edges", "--max-options",
                                          "--seed", "--output"};
    const pbqp_tools::Arguments arguments(argc, argv, names);
    if (arguments.help()) {
      help();
      return 0;
    }
    if (!arguments.positional().empty())
      throw std::invalid_argument(
          "use named options for all generation parameters");
    const pbqp_tools::GeneratorOptions options{
        number<size_t>("--nodes", arguments.require("--nodes")),
        number<size_t>("--edges", arguments.require("--edges")),
        number<size_t>("--max-options", arguments.require("--max-options")),
        number<std::uint64_t>("--seed", arguments.require("--seed"))};
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
