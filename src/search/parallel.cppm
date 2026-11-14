/*
 * Private synchronous oneTBB consumer-dispatch declaration.
 *
 * The bridge borrows a callable context until every task has joined. A small
 * adapter avoids owning type erasure and keeps scheduler headers and types out
 * of module partitions and their template instantiations.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include <cstddef>

module pbqp:parallel;

namespace pbqp {
std::size_t parallel_concurrency(std::size_t requested);
void run_parallel(std::size_t count, void *context,
                  void (*consume)(void *, std::size_t),
                  void (*cancel)(void *) noexcept = nullptr);

template <typename Consumer>
void run_parallel(std::size_t count, Consumer consumer) {
  run_parallel(
      count, &consumer,
      [](void *context, std::size_t index) {
        (*static_cast<Consumer *>(context))(index);
      },
      nullptr);
}

// The failure callback wakes consumers blocked in a dynamic work queue before
// the scheduler waits for them. The context remains borrowed until all join.
template <typename Consumer, typename Cancel>
void run_parallel(std::size_t count, Consumer consumer, Cancel cancel) {
  struct Context {
    Consumer &consumer;
    Cancel &cancel;
  } context{consumer, cancel};

  run_parallel(
      count, &context,
      [](void *raw, std::size_t index) {
        static_cast<Context *>(raw)->consumer(index);
      },
      [](void *raw) noexcept { static_cast<Context *>(raw)->cancel(); });
}

} // namespace pbqp
