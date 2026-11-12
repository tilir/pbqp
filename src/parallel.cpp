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
#include <cstddef>
#include <functional>
#include <oneapi/tbb/task_arena.h>
#include <oneapi/tbb/task_group.h>

module pbqp;
import :parallel;

namespace pbqp {
void run_parallel(std::size_t count,
                  const std::function<void(std::size_t)> &consume) {
  if (!count)
    return;
  if (count == 1) {
    consume(0);
    return;
  }
  oneapi::tbb::task_arena arena(static_cast<int>(count));
  arena.execute([&] {
    oneapi::tbb::task_group group;
    for (std::size_t id = 0; id < count; ++id)
      group.run([&, id] { consume(id); });
    group.wait();
  });
}
} // namespace pbqp
