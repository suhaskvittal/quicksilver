/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#include <algorithm>
#include <bit>
#include <numeric>

#include <strings.h>

#define TEMPL_PARAMS    template <size_t W, class WORD_TYPE>
#define TEMPL_CLASS     FIXED_POINT<W, WORD_TYPE>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class ITER_TYPE> TEMPL_PARAMS
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
        backing_array_[word_idx] |= (1 << bit_idx);
    else
        backing_array_[word_idx] &= ~(1 << bit_idx);
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

TEMPL_PARAMS size_t
TEMPL_CLASS::popcount() const
{
    size_t count = std::reduce(backing_array_.begin(), backing_array_.end(), static_cast<size_t>(0),
                            [] (size_t sum, word_type w) { return sum + std::popcount(w); });
    return count;
}

TEMPL_PARAMS std::pair<size_t, size_t>
TEMPL_CLASS::get_word_and_bit_idx(size_t idx) const
{
    return {idx / BITS_PER_WORD, idx % BITS_PER_WORD};
}

TEMPL_PARAMS typename TEMPL_CLASS::index_pair
TEMPL_CLASS::msb() const
{
    // use flsll to find the msb:
    auto flsll_generic = [] (word_type w) { return flsll(*(long long*)&w); }

    // find the last nonzero word
    auto nz_it = std::find_if(backing_array_.rbegin(), backing_array_.rend(), [] (word_type w) { return w != 0; });
    if (nz_it == backing_array_.rend())
        return -1;

    // find msb:
    size_t bit_idx = flsll_generic(*nz_it)-1;
    size_t word_idx = NUM_WORDS - std::distance(backing_array_.rbegin(), nz_it) - 1;
    return {word_idx, bit_idx};
}

TEMPL_PARAMS typename TEMPL_CLASS::index_pair
TEMPL_CLASS::lsb() const
{
    // use ffsll to find the lsb:
    auto ffsll_generic = [] (word_type w) { return ffsll(*(long long*)&w); }

    // find the first nonzero word
    auto nz_it = std::find_if(backing_array_.begin(), backing_array_.end(), [] (word_type w) { return w != 0; });
    if (nz_it == backing_array_.end())
        return -1;

    // find lsb:
    size_t bit_idx = ffsll_generic(*nz_it) - 1;
    size_t word_idx = std::distance(backing_array_.begin(), nz_it);
    return {word_idx, bit_idx};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////