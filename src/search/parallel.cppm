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
void run_parallel(std::size_t count, void *context,
                  void (*consume)(void *, std::size_t));

template <class Consumer>
void run_parallel(std::size_t count, Consumer consumer) {
  run_parallel(count, &consumer, [](void *context, std::size_t index) {
    (*static_cast<Consumer *>(context))(index);
  });
}
} // namespace pbqp
