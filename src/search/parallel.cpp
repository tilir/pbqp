/*
 * oneTBB scheduler bridge shared by exact solvers.
 *
 * Keeps TBB headers and task bodies in an implementation unit: GCC cannot
 * serialize some TBB translation-unit-local helpers into module partitions.
 * A single consumer executes directly without invoking the scheduler.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include <algorithm>
#include <cstddef>
#include <exception>
#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/info.h>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/task_group.h>

module pbqp;
import :parallel;

namespace pbqp {
std::size_t parallel_concurrency(std::size_t requested) {
  if (requested <= 1)
    return requested;
  auto available = static_cast<std::size_t>(
      std::max(1, oneapi::tbb::info::default_concurrency()));
  available = std::min(
      available, oneapi::tbb::global_control::active_value(
                     oneapi::tbb::global_control::max_allowed_parallelism));
  return std::min(requested, std::max(std::size_t{1}, available));
}

void run_parallel(std::size_t count, void *context,
                  void (*consume)(void *, std::size_t),
                  void (*cancel)(void *) noexcept) {
  if (!count)
    return;
  if (count == 1) {
    consume(context, 0);
    return;
  }
  oneapi::tbb::task_arena arena(static_cast<int>(count));
  arena.execute([&] {
    oneapi::tbb::task_group group;
    auto invoke = [&](std::size_t index) {
      try {
        consume(context, index);
      } catch (...) {
        if (cancel)
          cancel(context);
        throw;
      }
    };
    try {
      for (std::size_t id = 1; id < count; ++id)
        group.run([&, id] { invoke(id); });
      invoke(0);
      group.wait();
    } catch (...) {
      // Cancel the shared queue before joining: group cancellation alone does
      // not wake a consumer already asleep on a queue condition variable.
      auto failure = std::current_exception();
      if (cancel)
        cancel(context);
      group.cancel();
      try {
        group.wait();
      } catch (...) {
      }
      std::rethrow_exception(failure);
    }
  });
}
} // namespace pbqp
