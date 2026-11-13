/*
 * Executable entry point for private search preparation regressions.
 *
 * The test-only bridge exposes no implementation types or public C API changes.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
#include <exception>
#include <iostream>

namespace pbqp {
void run_search_internal_tests();
}

int main() {
  try {
    pbqp::run_search_internal_tests();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
