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

template <size_t W, class WORD_TYPE=uint64_t>
class FIXED_POINT
{
public:
    using word_type = WORD_TYPE;
    using index_pair = std::pair<int, int>;

    constexpr static size_t NUM_BITS{W},
                            BITS_PER_WORD{sizeof(WORD_TYPE)*8},
                            NUM_WORDS{W / BITS_PER_WORD};
private:
    std::array<WORD_TYPE, NUM_WORDS> backing_array_{};
public:
    constexpr FIXED_POINT() =default;
    constexpr FIXED_POINT(const FIXED_POINT&) =default;
    constexpr FIXED_POINT(WORD_TYPE w) :backing_array_{w} {}
    constexpr FIXED_POINT(std::array<WORD_TYPE, NUM_WORDS> x) :backing_array_(x) {}
    
    // this is useful for converting between fixed point widths quickly
    template <size_t _W> constexpr FIXED_POINT(FIXED_POINT<_W>);

    template <class ITER_TYPE> constexpr FIXED_POINT(ITER_TYPE begin, ITER_TYPE end);

    // bit-level operations:
    constexpr void set(size_t idx, bool);
    constexpr bool test(size_t idx) const;

    // word-level operations:
    constexpr void set_word(size_t idx, WORD_TYPE);
    constexpr word_type test_word(size_t idx) const;

    // bulk word-level operations:
    template <class XFORM_TYPE> constexpr void transform(const XFORM_TYPE&, size_t from=0, size_t to=NUM_WORDS);

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

    constexpr bool operator==(const FIXED_POINT&) const;
    constexpr bool operator!=(const FIXED_POINT&) const;

    constexpr std::array<word_type, NUM_WORDS> get_words() const { return backing_array_; }
    constexpr const std::array<word_type, NUM_WORDS>& get_words_ref() { return backing_array_; }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "fixed_point.tpp"

#endif // FIXED_POINT_h