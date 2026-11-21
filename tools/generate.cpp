/*
 * Random problem generator command-line entry point.
 *
 * Parses named parameters and writes compact PBQP 2 through the C API.
 * Records reproducible sampling identity and the quoted invocation as comments.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
#include "generator_version.hpp"
#include "pbqp.h"
#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <system_error>
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

std::string generator_identity() {
  return std::string("pbqp_generate ") + pbqp_tools::build::release + "+" +
         pbqp_tools::build::fingerprint;
}

std::string shell_quote(std::string_view argument) {
  const auto safe = [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' ||
           c == '/' || c == '=' || c == ':';
  };
  if (!argument.empty() && std::all_of(argument.begin(), argument.end(), safe))
    return std::string(argument);
  // Bash ANSI-C quoting preserves even control characters on one comment line.
  std::string result = "$'";
  for (unsigned char c : argument) {
    if (c == '\\' || c == '\'') {
      result += '\\';
      result += static_cast<char>(c);
    } else if (c < 32 || c == 127) {
      result += '\\';
      result += static_cast<char>('0' + (c >> 6));
      result += static_cast<char>('0' + ((c >> 3) & 7));
      result += static_cast<char>('0' + (c & 7));
    } else
      result += static_cast<char>(c);
  }
  result += '\'';
  return result;
}

// An exclusively claimed sibling directory keeps the staging path private.
// Rename publishes task and provenance together; failure preserves the old
// file.
class StagedOutput {
  std::filesystem::path target, directory, temporary;
  std::filesystem::perms access = std::filesystem::perms::unknown;
  bool owned = false;

public:
  explicit StagedOutput(std::string_view path) : target(path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(target, error);
    if (error && error != std::errc::no_such_file_or_directory)
      throw std::filesystem::filesystem_error("cannot inspect output", target,
                                              error);
    if (std::filesystem::exists(status) &&
        !std::filesystem::is_regular_file(status))
      throw std::invalid_argument("generator output must be a regular file, "
                                  "not a symlink or special file");
    if (std::filesystem::is_regular_file(status))
      access = status.permissions() & std::filesystem::perms::all;
    auto parent = target.parent_path();
    if (parent.empty())
      parent = ".";
    std::random_device names;
    constexpr unsigned attempts = 128;
    for (unsigned attempt = 0; attempt < attempts; ++attempt) {
      directory = parent / (".pbqp-generate-" + std::to_string(names()) + "-" +
                            std::to_string(attempt));
      temporary = directory / "problem.pbqp";
      // Restrict access at creation, before writing task data.
      if (::mkdir(directory.c_str(), S_IRWXU) == 0) {
        owned = true;
        return;
      }
      error = {errno, std::generic_category()};
      if (error != std::errc::file_exists)
        throw std::filesystem::filesystem_error(
            "cannot create staging directory", directory, error);
    }
    throw std::runtime_error("cannot claim a generator staging directory");
  }

  ~StagedOutput() noexcept {
    if (owned) {
      std::error_code ignored;
      std::filesystem::remove_all(directory, ignored);
    }
  }

  StagedOutput(const StagedOutput &) = delete;
  StagedOutput &operator=(const StagedOutput &) = delete;

  const std::filesystem::path &path() const noexcept { return temporary; }

  void publish() {
    if (access != std::filesystem::perms::unknown)
      std::filesystem::permissions(temporary, access);
    std::filesystem::rename(temporary, target);
  }
};

void provenance(std::string_view path, int argc, char **argv) {
  std::ofstream output(std::string(path), std::ios::binary | std::ios::app);
  if (!output)
    throw std::runtime_error("cannot open output for generator metadata");
  output << "# generator: " << generator_identity() << '\n'
         << "# random: mt19937_64; sampling protocol "
         << pbqp_tools::generation_protocol << '\n'
         << "# command:";
  for (int id = 0; id < argc; ++id)
    output << ' ' << shell_quote(argv[id]);
  output << '\n';
  output.close();
  if (!output)
    throw std::runtime_error("generator metadata write failed");
}

void help() {
  std::cout
      << "Usage: pbqp_generate --nodes=N --edges=M --seed=S --output=FILE "
         "[options]\n"
         "       pbqp_generate --help\n"
         "       pbqp_generate --version\n\n"
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
         "space-separated values.\n"
         "  -Werror  Treat unrecognized-option warnings as errors.\n";
}
} // namespace

int main(int argc, char **argv) {
  try {
    constexpr std::string_view names[] = {
        "--nodes",     "--edges",          "--max-options",
        "--seed",      "--output",         "--mode",
        "--registers", "--spill-cost-min", "--spill-cost-max"};
    constexpr std::string_view flags[] = {"-Werror", "--version"};
    const pbqp_tools::Arguments arguments(argc, argv, names, flags);
    arguments.report_warnings(std::cerr, arguments.has("-Werror"));
    if (arguments.help()) {
      help();
      return 0;
    }
    if (arguments.has("--version")) {
      std::cout << generator_identity()
                << "\nsampling protocol: " << pbqp_tools::generation_protocol
                << '\n';
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
        arguments.unsupported(std::cerr, "--max-options");
    } else if (mode == "random" || mode == "planted") {
      options.family = mode == "random" ? pbqp_tools::GeneratorFamily::Random
                                        : pbqp_tools::GeneratorFamily::Planted;
      options.max_options =
          number<size_t>("--max-options", arguments.require("--max-options"));
      for (auto name : {"--registers", "--spill-cost-min", "--spill-cost-max"})
        if (arguments.has(name))
          arguments.unsupported(std::cerr, name);
    } else
      throw std::invalid_argument("unknown generator mode: " +
                                  std::string(mode));
    auto output = arguments.require("--output");
    const auto generated = pbqp_tools::generate_problem(options);
    auto problem = generated.view();
    StagedOutput staged(output);
    const auto temporary = staged.path().string();
    pbqp_error error{};
    if (pbqp_write(temporary.c_str(), &problem, &error) != PBQP_OK) {
      std::cerr << error.message << '\n';
      return 1;
    }
    provenance(temporary, argc, argv);
    staged.publish();
  } catch (const std::exception &error) {
    std::cerr << error.what() << "\nUse --help for usage.\n";
    return 1;
  }
  return 0;
}
