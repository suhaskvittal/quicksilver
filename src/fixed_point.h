/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#ifndef FIXED_POINT_h
#define FIXED_POINT_h

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W, class Word=uint64_t>
class FixedPoint
{
public:
    using word_type = Word;
    using index_pair = std::pair<int, int>;

    constexpr static size_t NUM_BITS{W},
                            BITS_PER_WORD{sizeof(Word)*8},
                            NUM_WORDS{W / BITS_PER_WORD};
private:
    std::array<Word, NUM_WORDS> backing_array_{};
public:
    constexpr FixedPoint() =default;
    constexpr FixedPoint(const FixedPoint&) =default;
    constexpr FixedPoint(Word w) :backing_array_{w} {}
    constexpr FixedPoint(std::array<Word, NUM_WORDS> x) :backing_array_(x) {}
    
    // this is useful for converting between fixed point widths quickly
    template <size_t _W> constexpr FixedPoint(FixedPoint<_W>);

    template <class IterType> constexpr FixedPoint(IterType begin, IterType end);

    // bit-level operations:
    constexpr void set(size_t idx, bool);
    constexpr bool test(size_t idx) const;

    // word-level operations:
    constexpr void set_word(size_t idx, Word);
    constexpr word_type test_word(size_t idx) const;

    // bulk word-level operations:
    template <class Xform> constexpr void transform(const Xform&, size_t from=0, size_t to=NUM_WORDS);

    // bit shift operations:
    constexpr void lshft(int);
    constexpr void rshft(int);

    // word shift operations:
    constexpr void lshft_w(int);
    constexpr void rshft_w(int);

    // other useful operations:
    constexpr size_t popcount() const;
    constexpr int        join_word_and_bit_idx(index_pair) const;
    constexpr index_pair get_word_and_bit_idx(size_t idx) const;
    constexpr index_pair msb() const;   // returns {-1, -1} if all bits are 0
    constexpr index_pair lsb() const;   // returns {-1, -1} if all bits are 0

    std::string to_hex_string() const;

    constexpr bool operator==(const FixedPoint&) const;
    constexpr bool operator!=(const FixedPoint&) const;

    constexpr std::array<word_type, NUM_WORDS> get_words() const { return backing_array_; }
    constexpr const std::array<word_type, NUM_WORDS>& get_words_ref() { return backing_array_; }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "fixed_point.tpp"

#endif // FIXED_POINT_h