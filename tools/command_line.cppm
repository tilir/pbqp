/*
 * Named command-line argument parsing for PBQP tools.
 *
 * Accepts --name=value and --name value, rejects duplicate and unknown
 * options, and preserves positional paths after the -- separator.
 * Argument views borrow argv storage for the lifetime of the tools' main call.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;

#include <algorithm>
#include <map>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

export module pbqp.command_line;

export namespace pbqp_tools {
class Arguments {
  std::map<std::string_view, std::string_view> options;
  std::vector<std::string_view> paths;
  bool help_requested = false;

public:
  Arguments(int argc, char **argv, std::span<const std::string_view> names) {
    bool positional_only = false;
    for (int i = 1; i < argc; ++i) {
      std::string_view argument(argv[i]);
      if (!positional_only && argument == "--") {
        positional_only = true;
      } else if (!positional_only &&
                 (argument == "--help" || argument == "-h")) {
        help_requested = true;
      } else if (!positional_only && argument.starts_with("--")) {
        auto equals = argument.find('=');
        auto name = argument.substr(0, equals);
        if (std::find(names.begin(), names.end(), name) == names.end())
          throw std::invalid_argument("unknown option: " + std::string(name));
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
        if (!options.emplace(name, value).second)
          throw std::invalid_argument("duplicate option: " + std::string(name));
      } else {
        paths.push_back(argument);
      }
    }
  }

  bool help() const noexcept { return help_requested; }
  const std::vector<std::string_view> &positional() const noexcept {
    return paths;
  }
  std::string_view require(std::string_view name) const {
    auto option = options.find(name);
    if (option == options.end())
      throw std::invalid_argument("missing required option: " +
                                  std::string(name));
    return option->second;
  }
};
} // namespace pbqp_tools
