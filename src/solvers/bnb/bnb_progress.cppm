/*
 * Approximate B&B progress over branches surviving bound checks.
 *
 * Workers split subtree weights between viable children, excluding immediate
 * cuts, and batch completed weights. Empty initial/donated jobs are removed
 * from the denominator. Only successful exhaustion reports the final percent;
 * neither subtree weights nor their floating-point sum predict elapsed time.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <algorithm>
#include <mutex>

module pbqp:bnb_progress;

namespace pbqp {
class BranchAndBoundProgress {
  pbqp_progress_callback callback;
  void *user_data;
  std::mutex mutex;
  double completed = 0, discarded = 0;
  unsigned next_percent = 1;

  void report(unsigned percent) noexcept {
    while (next_percent <= percent)
      callback(next_percent++, user_data);
  }
  void merge(double work, double skipped) noexcept {
    std::scoped_lock lock(mutex);
    completed += work;
    discarded += skipped;
    const double remaining = 1 - discarded;
    // Rounding, underestimated subtree sizes, or a final worker's pending
    // batch must never announce completion before all consumers have joined.
    const double fraction = remaining > 0 ? completed / remaining : 0;
    report(static_cast<unsigned>(std::clamp(100 * fraction, 0.0, 99.0)));
  }

public:
  class Accumulator {
    BranchAndBoundProgress &owner;
    double work = 0, skipped = 0;
    unsigned events = 0;

    void add(double weight, bool discard) noexcept {
      (discard ? skipped : work) += weight;
      if (++events == 256 || weight >= 0.01)
        flush();
    }

  public:
    explicit Accumulator(BranchAndBoundProgress &p) : owner(p) {}
    void completed(double weight) noexcept { add(weight, false); }
    void discarded(double weight) noexcept { add(weight, true); }
    void flush() noexcept {
      if (!events)
        return;
      owner.merge(work, skipped);
      work = skipped = 0;
      events = 0;
    }
  };

  BranchAndBoundProgress(pbqp_progress_callback cb, void *data)
      : callback(cb), user_data(data) {}
  void finish() noexcept {
    std::scoped_lock lock(mutex);
    report(100);
  }
};
} // namespace pbqp
