/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#include <algorithm>
#include <bit>
#include <numeric>
#include <sstream>

#include <strings.h>

#define TEMPL_PARAMS    template <size_t W, class WORD_TYPE>
#define TEMPL_CLASS     FIXED_POINT<W, WORD_TYPE>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS template <class ITER_TYPE>
TEMPL_CLASS::FIXED_POINT(ITER_TYPE begin, ITER_TYPE end)
{
    std::copy(begin, end, backing_array_.begin());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS void
TEMPL_CLASS::set(size_t idx, bool value)
{
    auto [word_idx, bit_idx] = get_word_and_bit_idx(idx);
    if (value)
        backing_array_[word_idx] |= (word_type{1} << bit_idx);
    else
        backing_array_[word_idx] &= ~(word_type{1} << bit_idx);
}

TEMPL_PARAMS bool
TEMPL_CLASS::test(size_t idx) const
{
    auto [word_idx, bit_idx] = get_word_and_bit_idx(idx);
    return (backing_array_[word_idx] >> bit_idx) & 1;
}

TEMPL_PARAMS void
TEMPL_CLASS::set_word(size_t idx, word_type w)
{
    backing_array_[idx] = w;
}

TEMPL_PARAMS typename TEMPL_CLASS::word_type
TEMPL_CLASS::test_word(size_t idx) const
{
    return backing_array_[idx];
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS void
TEMPL_CLASS::lshft(int n)
{
    if (n < 0)
        return rshft(-n);

    for (ssize_t i = NUM_WORDS-1; i > 0; i--)
    {
        backing_array_[i] <<= n;
        // now the problem is that the previous word will send `n` bits to this word.
        backing_array_[i] |= (backing_array_[i-1] >> (BITS_PER_WORD-n));
    }
    backing_array_[0] <<= n;
}

TEMPL_PARAMS void
TEMPL_CLASS::rshft(int n)
{
    if (n < 0)
        return lshft(-n);

    for (ssize_t i = 0; i < NUM_WORDS-1; i++)
    {
        backing_array_[i] >>= n;
        word_type next_word_bits = backing_array_[i+1] & ((word_type{1}<<n)-1);
        backing_array_[i] |= next_word_bits << (BITS_PER_WORD-n);
    }
    backing_array_[NUM_WORDS-1] >>= n;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS size_t
TEMPL_CLASS::popcount() const
{
    size_t count = std::transform_reduce(backing_array_.begin(), backing_array_.end(), 
                                        size_t{0},
                                        std::plus<size_t>{},
                                        [] (word_type w) { return std::popcount(w); });
    return count;
}

TEMPL_PARAMS int
TEMPL_CLASS::join_word_and_bit_idx(index_pair idx) const
{
    if (idx.first < 0 || idx.second < 0)
        return -1;
    else
        return idx.first * BITS_PER_WORD + idx.second;
}

TEMPL_PARAMS typename TEMPL_CLASS::index_pair
TEMPL_CLASS::get_word_and_bit_idx(size_t idx) const
{
    return {idx / BITS_PER_WORD, idx % BITS_PER_WORD};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS typename TEMPL_CLASS::index_pair
TEMPL_CLASS::msb() const
{
    // Constexpr lookup table for bit reversal in a byte
    // This table contains the bit-reversed value for each possible byte value (0-255)
    constexpr std::array<uint8_t, 256> BIT_REVERSE_TABLE = 
    []() constexpr 
    {
        std::array<uint8_t, 256> table{};
        for (size_t i = 0; i < 256; ++i) {
            uint8_t reversed = 0;
            for (size_t bit = 0; bit < 8; ++bit) {
                if (i & (1 << bit)) {
                    reversed |= (1 << (7 - bit));
                }
            }
            table[i] = reversed;
        }
        return table;
    }();
    
    // use flsll to find the msb -- note that while `ffs` is guaranteed, `fls` is not
    auto flsll_generic = [&BIT_REVERSE_TABLE] (word_type w) 
                        {
                            // traverse `w` in reverse order, 4B at a time
                            constexpr size_t WORD_SIZE = sizeof(word_type);
                            constexpr size_t DELTA = sizeof(int);
                            for (ssize_t i = WORD_SIZE-DELTA; i >= 0; i -= DELTA)
                            {
                                // reconstruct 4B in reverse and apply `ffs` (4B find-first-set)
                                int rev{0};
                                for (ssize_t j = 0; j < DELTA; j++)
                                {
                                    size_t k = (i+j)<<3,
                                           _k = (WORD_SIZE-i-j-1)<<3;
                                    uint8_t byte = (w >> k) & 0xff;
                                    rev |= BIT_REVERSE_TABLE[byte] << _k;
                                }
                                ssize_t msb_idx = ffs(rev);
                                if (msb_idx > 0)
                                {
                                    msb_idx = 8*DELTA-msb_idx+1;
                                    return msb_idx + (i<<3);
                                }
                            }
                            return ssize_t{0};
                        };

    // find the last nonzero word
    auto nz_it = std::find_if(backing_array_.rbegin(), backing_array_.rend(), [] (word_type w) { return w != 0; });
    if (nz_it == backing_array_.rend())
        return {-1, -1};

    // find msb:
    size_t bit_idx = flsll_generic(*nz_it)-1;
    size_t word_idx = NUM_WORDS - std::distance(backing_array_.rbegin(), nz_it) - 1;
    return {word_idx, bit_idx};
}

TEMPL_PARAMS typename TEMPL_CLASS::index_pair
TEMPL_CLASS::lsb() const
{
    // use ffsll to find the lsb:
    auto ffsll_generic = [] (word_type w) { return ffsll(*(long long*)&w); };

    // find the first nonzero word
    auto nz_it = std::find_if(backing_array_.begin(), backing_array_.end(), [] (word_type w) { return w != 0; });
    if (nz_it == backing_array_.end())
        return {-1, -1};

    // find lsb:
    size_t bit_idx = ffsll_generic(*nz_it) - 1;
    size_t word_idx = std::distance(backing_array_.begin(), nz_it);
    return {word_idx, bit_idx};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS std::string
TEMPL_CLASS::to_hex_string() const
{
    std::stringstream ss;

    for (size_t i = 0; i < NUM_WORDS; i++)
    {
        word_type w = backing_array_[i];
        for (size_t j = 0; j < BITS_PER_WORD; j += 4)
        {
            word_type nibble = (w >> j) & 0xf;
            ss << std::hex << nibble;
        }
        ss << " ";
    }

    std::string out = ss.str();
    std::reverse(out.begin(), out.end());
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#undef TEMPL_PARAMS
#undef TEMPL_CLASS