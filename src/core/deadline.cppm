/*
 * Shared monotonic, cooperative wall-time budget for synchronous operations.
 * Copies retain the same start time and can be read concurrently. No timer
 * thread, callback, exception, or asynchronous interruption is involved.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * See LICENSE for the full license text.
 */
module;
#include <chrono>
#include <cstddef>
#include <ratio>
module pbqp:deadline;

namespace pbqp {
class Deadline {
  using Clock = std::chrono::steady_clock;
  size_t milliseconds = 0;
  Clock::time_point started{};

public:
  explicit Deadline(size_t limit = 0) noexcept : milliseconds(limit) {
    if (enabled())
      started = Clock::now();
  }

  bool enabled() const noexcept { return milliseconds != 0; }

  bool expired() const noexcept {
    if (!enabled())
      return false;
    // Comparing elapsed time avoids overflowing time_point when the caller
    // supplies a size_t budget larger than the clock's representable duration.
    const std::chrono::duration<long double, std::milli> elapsed =
        Clock::now() - started;
    return elapsed.count() >= static_cast<long double>(milliseconds);
  }
};
} // namespace pbqp
