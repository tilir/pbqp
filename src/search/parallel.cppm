/*
 * Private synchronous oneTBB consumer-dispatch declaration.
 *
 * Keeps third-party scheduler types outside serialized module interfaces.
 * The implementation joins every consumer before returning to an exact solver.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include <cstddef>
#include <functional>

module pbqp:parallel;

namespace pbqp {
void run_parallel(std::size_t count,
                  const std::function<void(std::size_t)> &consume);
} // namespace pbqp
