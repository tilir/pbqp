/*
 * PBQP text token reader.
 *
 * Implements a stream-backed reader for keywords, dimensions, and costs.
 * Tracks input lines and handles whitespace and hash comments.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;

#include "pbqp.h"
#include <cctype>
#include <charconv>
#include <cmath>
#include <istream>
#include <string>
#include <string_view>

module pbqp:text_reader;
import :failure;

namespace pbqp {
class TextReader {
  std::istream &file;
  size_t line = 1;
  size_t current_token_line = 1;
  std::string token;

  void required();
  int get();
  int comment();

public:
  explicit TextReader(std::istream &input) : file(input) {}

  size_t token_line() const noexcept { return current_token_line; }

  bool next();

  // Borrowed until the next token is read.
  std::string_view word() {
    required();
    return token;
  }

  void expect(const char *word);
  size_t size();
  pbqp_cost_t cost();
};

bool TextReader::next() {
  token.clear();
  int c;
  for (;;) {
    c = get();
    if (c == '#')
      c = comment();
    if (c == std::char_traits<char>::eof())
      return false;
    if (!std::isspace(static_cast<unsigned char>(c)))
      break;
    if (c == '\n')
      ++line;
  }
  current_token_line = line;
  do {
    if (c == 0)
      fail(PBQP_INVALID, "NUL byte in input token", current_token_line);
    token += static_cast<char>(c);
    c = get();
  } while (c != std::char_traits<char>::eof() && c != '#' &&
           !std::isspace(static_cast<unsigned char>(c)));
  if (c == '#')
    c = comment();
  if (c == '\n')
    ++line;
  return true;
}

void TextReader::required() {
  if (!next())
    fail(PBQP_INVALID, "unexpected end of input", line);
}

void TextReader::expect(const char *word) {
  required();
  if (token != word)
    fail(PBQP_INVALID, "unexpected keyword or format version",
         current_token_line);
}

size_t TextReader::size() {
  required();
  for (char c : token)
    if (c < '0' || c > '9')
      fail(PBQP_INVALID, "expected nonnegative decimal integer",
           current_token_line);
  size_t value = 0;
  auto result =
      std::from_chars(token.data(), token.data() + token.size(), value);
  if (result.ec == std::errc::result_out_of_range)
    fail(PBQP_OVERFLOW, "integer exceeds size_t range", current_token_line);
  if (result.ec != std::errc{})
    fail(PBQP_INVALID, "invalid integer", current_token_line);
  return value;
}

pbqp_cost_t TextReader::cost() {
  required();
  if (token == "INF")
    return PBQP_INF;
  const char *begin = token.data();
  const char *end = begin + token.size();
  if (*begin == '+') {
    ++begin;
    if (begin == end || *begin == '+' || *begin == '-')
      fail(PBQP_INVALID, "expected one optional sign before cost",
           current_token_line);
  }
  pbqp_cost_t value = 0;
  auto result = std::from_chars(begin, end, value, std::chars_format::general);
  if (result.ec == std::errc::result_out_of_range)
    fail(PBQP_OVERFLOW, "cost outside representable float range",
         current_token_line);
  if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(value))
    fail(PBQP_INVALID, "expected finite decimal cost or INF",
         current_token_line);
  return value;
}

int TextReader::get() {
  int c = file.get();
  if (c == std::char_traits<char>::eof() &&
      (file.bad() || (file.fail() && !file.eof())))
    fail(PBQP_IO_ERROR, "input read failed", line);
  return c;
}

int TextReader::comment() {
  int c;
  do {
    c = get();
  } while (c != '\n' && c != std::char_traits<char>::eof());
  return c;
}
} // namespace pbqp
