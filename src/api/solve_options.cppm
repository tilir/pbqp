/*
 * Validated typed views of the open C solver option map.
 * Only consumed keys inspect payloads; solvers may share maps containing
 * unrelated application settings. Parsing stays outside all search loops.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>
module pbqp:solve_options;
import :failure;
import :error;
import :diagnostics;

namespace pbqp {
class SolveOptionsView {
  std::span<const pbqp_option> entries;
  mutable std::vector<bool> consumed;

public:
  explicit SolveOptionsView(const pbqp_solve_options *options) {
    if (!options || !options->count)
      return;
    if (!options->entries)
      fail(PBQP_INVALID, "missing option entries");
    entries = {options->entries, options->count};
    consumed.resize(entries.size());
    for (const auto &entry : entries) {
      if (!entry.key || !*entry.key)
        fail(PBQP_INVALID, "option keys must be nonempty");
    }
  }

  ~SolveOptionsView() noexcept;

  const pbqp_option *find(std::string_view key, pbqp_option_type type) const {
    for (auto it = entries.rbegin(); it != entries.rend(); ++it)
      if (key == it->key) {
        const auto &entry = *it;
        consumed[static_cast<size_t>(&entry - entries.data())] = true;
        if (entry.type != type)
          fail(PBQP_INVALID, "wrong type for solver option");
        return &entry;
      }
    return nullptr;
  }

  pbqp_status check_unknown(Diagnostics warnings, bool errors,
                            pbqp_error *error) const {
    if (!warnings && !errors)
      return PBQP_OK;
    std::unordered_set<std::string_view> seen;
    for (size_t id = entries.size(); id-- > 0;) {
      if (!seen.insert(entries[id].key).second || consumed[id])
        continue;
      if (errors) {
        const auto message = std::string("unrecognized solver option: ") +
                             entries[id].key + " (warning treated as error)";
        return set_error(error, PBQP_INVALID, 0, message.c_str());
      }
      warnings.line("warning: unrecognized solver option: ", entries[id].key);
    }
    return PBQP_OK;
  }

  size_t size(std::string_view key, size_t fallback) const {
    auto entry = find(key, PBQP_OPTION_SIZE);
    return entry ? entry->value.size : fallback;
  }

  bool boolean(std::string_view key, bool fallback) const {
    auto entry = find(key, PBQP_OPTION_BOOL);
    if (!entry)
      return fallback;
    if (entry->value.boolean != 0 && entry->value.boolean != 1)
      fail(PBQP_INVALID, "boolean option must be zero or one");
    return entry->value.boolean != 0;
  }

  void *pointer(std::string_view key) const {
    auto entry = find(key, PBQP_OPTION_POINTER);
    return entry ? entry->value.pointer : nullptr;
  }

  pbqp_progress_callback progress() const {
    auto entry = find("progress", PBQP_OPTION_PROGRESS);
    return entry ? entry->value.progress : nullptr;
  }

  pbqp_diagnostic_callback
  diagnostics(std::string_view key = "diagnostics") const {
    auto entry = find(key, PBQP_OPTION_DIAGNOSTIC);
    return entry ? entry->value.diagnostic : nullptr;
  }
};

SolveOptionsView::~SolveOptionsView() noexcept = default;

struct CommonSolveOptions {
  SolveOptionsView view;
  size_t threads;
  pbqp_progress_callback progress;
  void *progress_data;
  Diagnostics log, warnings;
  bool warnings_as_errors;

  explicit CommonSolveOptions(const pbqp_solve_options *options)
      : view(options), threads(view.size("num-threads", 1)),
        progress(view.progress()), progress_data(view.pointer("progress-data")),
        log(view.diagnostics(), view.pointer("diagnostic-data")),
        warnings(view.diagnostics("warnings"), view.pointer("warning-data")),
        warnings_as_errors(view.boolean("warnings-as-errors", false)) {
    if (!threads ||
        threads > static_cast<size_t>(std::numeric_limits<int>::max()))
      fail(PBQP_INVALID, "num-threads must be positive and fit in int");
  }

  pbqp_status unsupported(const char *message, pbqp_error *error) const {
    if (warnings_as_errors) {
      const auto text = std::string(message) + " (warning treated as error)";
      return set_error(error, PBQP_INVALID, 0, text.c_str());
    }
    const auto sink = warnings ? warnings : log;
    sink.line("warning: ", message);
    return PBQP_OK;
  }

  pbqp_status check_heuristic(pbqp_error *error) const {
    if (progress) {
      auto status = unsupported(
          "progress/heartbeat is unsupported for heuristic solvers; ignored",
          error);
      if (status != PBQP_OK)
        return status;
    }
    if (threads > 1) {
      auto status = unsupported(
          "num-threads above 1 is unsupported for heuristic solvers; using 1",
          error);
      if (status != PBQP_OK)
        return status;
    }
    return check_unknown(error);
  }

  pbqp_status check_unknown(pbqp_error *error) const {
    return view.check_unknown(warnings ? warnings : log, warnings_as_errors,
                              error);
  }
};
} // namespace pbqp
