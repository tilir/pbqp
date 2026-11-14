/*
 * Named command-line argument parsing for PBQP tools.
 *
 * Accepts named values and flags, warns about unknown options, and preserves
 * positional paths after --. Callers choose duplicate rejection or last-wins.
 * Argument views borrow argv storage for the lifetime of the tools' main call.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;

#include <algorithm>
#include <map>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

export module pbqp.command_line;

export namespace pbqp_tools {
class Arguments {
  struct Option {
    std::string_view value;
    size_t position;
  };

  std::map<std::string_view, Option> options;
  std::vector<std::string_view> paths;
  std::vector<std::string_view> unknown;
  bool help_requested = false;

  void record(std::string_view name, std::string_view value, size_t position,
              bool last_wins) {
    if (!last_wins && options.contains(name))
      throw std::invalid_argument("duplicate option: " + std::string(name));
    options.insert_or_assign(name, Option{value, position});
  }

public:
  Arguments(int argc, char **argv, std::span<const std::string_view> names,
            std::span<const std::string_view> flags = {},
            bool last_wins = false) {
    bool positional_only = false;
    for (int i = 1; i < argc; ++i) {
      std::string_view argument(argv[i]);
      if (!positional_only && argument == "--") {
        positional_only = true;
      } else if (!positional_only &&
                 (argument == "--help" || argument == "-h")) {
        help_requested = true;
      } else if (!positional_only && argument.starts_with("-") &&
                 argument != "-") {
        auto equals = argument.find('=');
        auto name = argument.substr(0, equals);
        if (std::find(flags.begin(), flags.end(), name) != flags.end()) {
          if (equals != std::string_view::npos)
            throw std::invalid_argument("flag takes no value: " +
                                        std::string(name));
          record(name, {}, static_cast<size_t>(i), last_wins);
          continue;
        }
        if (std::find(names.begin(), names.end(), name) == names.end()) {
          unknown.push_back(name);
          continue;
        }
        std::string_view value;
        if (equals != std::string_view::npos) {
          value = argument.substr(equals + 1);
        } else {
          if (i + 1 == argc || std::string_view(argv[i + 1]).starts_with("--"))
            throw std::invalid_argument("missing value for " +
                                        std::string(name));
          value = argv[++i];
        }
        if (value.empty())
          throw std::invalid_argument("empty value for " + std::string(name));
        record(name, value, static_cast<size_t>(i), last_wins);
      } else {
        paths.push_back(argument);
      }
    }
  }

  void unsupported(std::ostream &output, std::string_view name) const {
    if (has("-Werror"))
      throw std::invalid_argument(
          "option does not apply to this mode: " + std::string(name) +
          " (warning treated as error by -Werror)");
    output << "warning: option does not apply to this mode; ignored: " << name
           << '\n';
  }

  void report_warnings(std::ostream &output, bool errors) const {
    if (errors && !unknown.empty())
      throw std::invalid_argument(
          "unrecognized option: " + std::string(unknown.front()) +
          " (warning treated as error by -Werror)");
    for (auto name : unknown)
      output << "warning: unrecognized option: " << name << '\n';
  }

  bool help() const noexcept { return help_requested; }

  const std::vector<std::string_view> &positional() const noexcept {
    return paths;
  }

  bool has(std::string_view name) const { return options.contains(name); }

  size_t last_position(std::string_view name) const {
    auto option = options.find(name);
    return option == options.end() ? 0 : option->second.position;
  }

  std::string_view get(std::string_view name, std::string_view fallback) const {
    auto option = options.find(name);
    return option == options.end() ? fallback : option->second.value;
  }

  std::string_view require(std::string_view name) const {
    auto option = options.find(name);
    if (option == options.end())
      throw std::invalid_argument("missing required option: " +
                                  std::string(name));
    return option->second.value;
  }
};
} // namespace pbqp_tools
