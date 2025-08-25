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

    // This cannot be constexpr because it requires std::copy
    template <class ITER_TYPE> FIXED_POINT(ITER_TYPE begin, ITER_TYPE end);

    // bit-level operations:
    void set(size_t idx, bool);
    bool test(size_t idx) const;

    // word-level operations:
    void set_word(size_t idx, WORD_TYPE);
    word_type test_word(size_t idx) const;

    // bit shift operations:
    void lshft(int);
    void rshft(int);

    // other useful operations:
    size_t popcount() const;
    int        join_word_and_bit_idx(index_pair) const;
    index_pair get_word_and_bit_idx(size_t idx) const;
    index_pair msb() const;   // returns {-1, -1} if all bits are 0
    index_pair lsb() const;   // returns {-1, -1} if all bits are 0

    std::string to_hex_string() const;

    bool operator==(const FIXED_POINT&) const;
    bool operator!=(const FIXED_POINT&) const;

    std::array<word_type, NUM_WORDS> get_words() const { return backing_array_; }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "fixed_point.tpp"

#endif // FIXED_POINT_h