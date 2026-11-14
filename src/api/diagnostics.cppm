/*
 * Optional synchronous C diagnostic sink for solver preparation decisions.
 * Formatting and callbacks happen outside DFS. With no sink, no message
 * storage is constructed and algorithms retain their normal execution path.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include "pbqp.h"
#include <ios>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
module pbqp:diagnostics;
namespace pbqp {
class Diagnostics {
  pbqp_diagnostic_callback callback = nullptr;
  void *user_data = nullptr;
  // Explicit text writes avoid nonmember stream-overload lookup across module
  // instantiations (GCC can otherwise select pointer insertion for literals).
  static void append(std::ostringstream &out, std::string_view text) {
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
  }
  template <typename Value>
    requires std::is_arithmetic_v<Value>
  static void append(std::ostringstream &out, Value value) {
    out << value;
  }

public:
  Diagnostics() = default;
  Diagnostics(pbqp_diagnostic_callback cb, void *data) noexcept
      : callback(cb), user_data(data) {}
  explicit operator bool() const noexcept { return callback != nullptr; }
  template <typename... Values> void line(const Values &...values) const {
    if (!callback)
      return;
    std::ostringstream output;
    output.imbue(std::locale::classic());
    output.precision(std::numeric_limits<pbqp_cost_t>::max_digits10);
    (append(output, values), ...);
    const auto message = output.str();
    callback(message.c_str(), user_data);
  }
};
} // namespace pbqp
