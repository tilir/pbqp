/*
 * Fixed-size-per-instance dynamic bit sets backed by standard word vectors.
 *
 * Word operations and set-bit iteration reuse preallocated storage. Bit counts
 * are unrestricted by the machine word width; padding bits always remain zero.
 * Explicit destination operations avoid allocation inside solver search loops.
 *
 * SPDX-License-Identifier: GPL-3.0-only
 * Distributed under GNU GPL version 3, WITHOUT ANY WARRANTY.
 * See LICENSE for the full license text.
 */
module;
#include <algorithm>
#include <bit>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <limits>
#include <type_traits>
#include <vector>

module pbqp:bitset;

namespace pbqp {
template <typename Word = unsigned>
  requires(std::unsigned_integral<Word> &&
           std::same_as<Word, std::remove_cv_t<Word>> &&
           requires(Word word) { std::popcount(word); })
class BitSet {
public:
  using word_type = Word;
  static constexpr std::size_t word_bits = std::numeric_limits<Word>::digits;

  static std::size_t storage_words(std::size_t bits) noexcept {
    return bits / word_bits + (bits % word_bits != 0);
  }

private:
  static constexpr Word all_bits = std::numeric_limits<Word>::max();
  std::size_t bits;
  std::vector<Word> words;

  void trim() noexcept {
    if (bits % word_bits)
      words.back() &= (Word{1} << (bits % word_bits)) - 1;
  }

  void compatible(const BitSet &other) const noexcept {
    assert(bits == other.bits);
    (void)other;
  }

public:
  explicit BitSet(std::size_t count = 0, bool filled = false)
      : bits(count), words(storage_words(count), filled ? all_bits : Word{0}) {
    trim();
  }

  // The range contains set-bit indices, not all bit positions. Its extent is
  // named separately so ranges::size cannot mistake it for the element count.
  std::size_t num_bits() const noexcept { return bits; }

  class const_iterator {
    const BitSet *owner = nullptr;
    std::size_t position = 0;

  public:
    using value_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    using reference = value_type;
    using pointer = void;
    using iterator_concept = std::forward_iterator_tag;
    using iterator_category = std::forward_iterator_tag;

    const_iterator() = default;

    const_iterator(const BitSet &set, std::size_t bit) noexcept
        : owner(&set), position(bit) {}

    value_type operator*() const noexcept {
      assert(owner && position < owner->num_bits());
      return position;
    }

    const_iterator &operator++() noexcept {
      assert(owner && position < owner->num_bits());
      position = owner->next(position);
      return *this;
    }

    const_iterator operator++(int) noexcept {
      auto previous = *this;
      ++*this;
      return previous;
    }

    bool operator==(const const_iterator &) const noexcept = default;
  };

  using iterator = const_iterator;

  const_iterator begin() const noexcept { return {*this, first()}; }

  const_iterator end() const noexcept { return {*this, bits}; }

  std::size_t word_count() const noexcept { return words.size(); }

  void clear() noexcept { std::fill(words.begin(), words.end(), Word{0}); }

  void fill() noexcept {
    std::fill(words.begin(), words.end(), all_bits);
    trim();
  }

  void set(std::size_t bit) noexcept {
    assert(bit < bits);
    words[bit / word_bits] |= Word{1} << (bit % word_bits);
  }

  void reset(std::size_t bit) noexcept {
    assert(bit < bits);
    words[bit / word_bits] &= ~(Word{1} << (bit % word_bits));
  }

  bool test(std::size_t bit) const noexcept {
    assert(bit < bits);
    return (words[bit / word_bits] & (Word{1} << (bit % word_bits))) != 0;
  }

  bool any() const noexcept {
    return std::any_of(words.begin(), words.end(),
                       [](Word word) { return word != 0; });
  }

  std::size_t count() const noexcept {
    std::size_t count = 0;
    for (Word word : words)
      count += std::popcount(word);
    return count;
  }

  std::size_t first() const noexcept {
    for (std::size_t id = 0; id < words.size(); ++id)
      if (words[id])
        return id * word_bits + std::countr_zero(words[id]);
    return bits;
  }

  std::size_t next(std::size_t previous) const noexcept {
    if (previous >= bits || previous + 1 == bits)
      return bits;
    std::size_t position = previous + 1, block = position / word_bits;
    Word word = words[block] & (all_bits << (position % word_bits));
    if (word)
      return block * word_bits + std::countr_zero(word);
    for (++block; block < words.size(); ++block)
      if (words[block])
        return block * word_bits + std::countr_zero(words[block]);
    return bits;
  }

  void copy_from(const BitSet &other) noexcept {
    compatible(other);
    if (this != &other)
      std::copy(other.words.begin(), other.words.end(), words.begin());
  }

  void intersect_with(const BitSet &other) noexcept {
    compatible(other);
    std::transform(words.begin(), words.end(), other.words.begin(),
                   words.begin(), std::bit_and<Word>{});
  }

  void subtract(const BitSet &other) noexcept {
    compatible(other);
    std::transform(words.begin(), words.end(), other.words.begin(),
                   words.begin(),
                   [](Word a, Word b) { return static_cast<Word>(a & ~b); });
  }

  void unite_with(const BitSet &other) noexcept {
    compatible(other);
    std::transform(words.begin(), words.end(), other.words.begin(),
                   words.begin(), std::bit_or<Word>{});
  }

  void assign_intersection(const BitSet &a, const BitSet &b) noexcept {
    compatible(a);
    compatible(b);
    std::transform(a.words.begin(), a.words.end(), b.words.begin(),
                   words.begin(), std::bit_and<Word>{});
  }

  void assign_difference(const BitSet &a, const BitSet &b) noexcept {
    compatible(a);
    compatible(b);
    std::transform(
        a.words.begin(), a.words.end(), b.words.begin(), words.begin(),
        [](Word lhs, Word rhs) { return static_cast<Word>(lhs & ~rhs); });
  }

  bool intersects(const BitSet &other) const noexcept {
    compatible(other);
    for (std::size_t id = 0; id < words.size(); ++id)
      if (words[id] & other.words[id])
        return true;
    return false;
  }

  std::size_t intersection_count(const BitSet &other) const noexcept {
    compatible(other);
    std::size_t count = 0;
    for (std::size_t id = 0; id < words.size(); ++id)
      count += std::popcount(static_cast<Word>(words[id] & other.words[id]));
    return count;
  }

  bool is_subset_of(const BitSet &other) const noexcept {
    compatible(other);
    for (std::size_t id = 0; id < words.size(); ++id)
      if (words[id] & ~other.words[id])
        return false;
    return true;
  }

  bool operator==(const BitSet &other) const noexcept {
    return bits == other.bits && words == other.words;
  }

  bool operator<(const BitSet &other) const noexcept {
    compatible(other);
    return std::lexicographical_compare(
        words.rbegin(), words.rend(), other.words.rbegin(), other.words.rend());
  }
};
} // namespace pbqp
