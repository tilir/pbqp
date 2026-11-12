/*
 * Internal PBQP text cost serialization.
 *
 * Formats explicit prohibitions as INF and finite costs through the caller's
 * configured stream precision and locale. Helpers remain private to pbqp.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <ostream>

module pbqp:text_writer;

namespace pbqp {
void write_cost(std::ostream &output, pbqp_cost_t cost) {
  if (cost == PBQP_INF)
    output << "INF";
  else
    output << cost;
}
} // namespace pbqp
