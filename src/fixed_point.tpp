/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#include <algorithm>
#include <bit>
#include <numeric>
#include <sstream>

#define TEMPL_PARAMS    template <size_t W, class WORD_TYPE>
#define TEMPL_CLASS     FIXED_POINT<W, WORD_TYPE>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS template <size_t _W>
constexpr TEMPL_CLASS::FIXED_POINT(FIXED_POINT<_W> x)
{
    std::copy(x.get_words_ref().begin(), x.get_words_ref().end(), backing_array_.begin());
}

TEMPL_PARAMS template <class ITER_TYPE>
constexpr TEMPL_CLASS::FIXED_POINT(ITER_TYPE begin, ITER_TYPE end)
{
    std::copy(begin, end, backing_array_.begin());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS constexpr void
TEMPL_CLASS::set(size_t idx, bool value)
{
    auto [word_idx, bit_idx] = get_word_and_bit_idx(idx);
    if (value)
        backing_array_[word_idx] |= (word_type{1} << bit_idx);
    else
        backing_array_[word_idx] &= ~(word_type{1} << bit_idx);
}

TEMPL_PARAMS constexpr bool
TEMPL_CLASS::test(size_t idx) const
{
    auto [word_idx, bit_idx] = get_word_and_bit_idx(idx);
    return (backing_array_[word_idx] >> bit_idx) & 1;
}

TEMPL_PARAMS constexpr void
TEMPL_CLASS::set_word(size_t idx, word_type w)
{
    backing_array_[idx] = w;
}

TEMPL_PARAMS constexpr typename TEMPL_CLASS::word_type
TEMPL_CLASS::test_word(size_t idx) const
{
    return backing_array_[idx];
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS template <class XFORM_TYPE> constexpr void
TEMPL_CLASS::transform(const XFORM_TYPE& xform, size_t from, size_t to)
{
    auto begin = backing_array_.begin() + from,
         end = backing_array_.begin() + to;

    std::transform(begin, end, begin, xform);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS constexpr void
TEMPL_CLASS::lshft(int n)
{
    if (n < 0)
        return rshft(-n);

    int num_word_shifts = n / BITS_PER_WORD;
    n %= BITS_PER_WORD;
    lshft_w(num_word_shifts);

    for (ssize_t i = NUM_WORDS-1; i > 0; i--)
    {
        backing_array_[i] <<= n;
        // now the problem is that the previous word will send `n` bits to this word.
        backing_array_[i] |= (backing_array_[i-1] >> (BITS_PER_WORD-n));
    }
    backing_array_[0] <<= n;
}

TEMPL_PARAMS constexpr void
TEMPL_CLASS::rshft(int n)
{
    if (n < 0)
        return lshft(-n);

    int num_word_shifts = n / BITS_PER_WORD;
    n %= BITS_PER_WORD;
    rshft_w(num_word_shifts);

    for (ssize_t i = 0; i < NUM_WORDS-1; i++)
    {
        backing_array_[i] >>= n;
        word_type next_word_bits = backing_array_[i+1] & ((word_type{1}<<n)-1);
        backing_array_[i] |= next_word_bits << (BITS_PER_WORD-n);
    }
    backing_array_[NUM_WORDS-1] >>= n;
}

TEMPL_PARAMS constexpr void
TEMPL_CLASS::lshft_w(int n)
{
    if (n < 0)
        return rshft_w(-n);

    // note that the lower entries of the array are the most significant words (so our "left" is STL's "right")
    std::shift_right(backing_array_.begin(), backing_array_.end(), n);
    std::fill(backing_array_.begin(), backing_array_.begin()+n, 0);
}

TEMPL_PARAMS constexpr void
TEMPL_CLASS::rshft_w(int n)
{
    if (n < 0)
        return lshft_w(-n);

    std::shift_left(backing_array_.begin(), backing_array_.end(), n);
    std::fill(backing_array_.end()-n, backing_array_.end(), 0);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS constexpr size_t
TEMPL_CLASS::popcount() const
{
    size_t count = 0;
    for (auto w : backing_array_)
        count += std::popcount(w);
    return count;
}

TEMPL_PARAMS constexpr int
TEMPL_CLASS::join_word_and_bit_idx(index_pair idx) const
{
    if (idx.first < 0 || idx.second < 0)
        return -1;
    else
        return idx.first * BITS_PER_WORD + idx.second;
}

TEMPL_PARAMS constexpr typename TEMPL_CLASS::index_pair
TEMPL_CLASS::get_word_and_bit_idx(size_t idx) const
{
    return {idx / BITS_PER_WORD, idx % BITS_PER_WORD};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS constexpr typename TEMPL_CLASS::index_pair
TEMPL_CLASS::msb() const
{
    auto nz_it = std::find_if(backing_array_.rbegin(), backing_array_.rend(), [] (word_type w) { return w != 0; });
    if (nz_it == backing_array_.rend())
        return {-1, -1};
    size_t bit_idx = BITS_PER_WORD - 1 - std::countl_zero(*nz_it);
    size_t word_idx = NUM_WORDS - std::distance(backing_array_.rbegin(), nz_it) - 1;
    return {word_idx, bit_idx};
}

TEMPL_PARAMS constexpr typename TEMPL_CLASS::index_pair
TEMPL_CLASS::lsb() const
{
    auto nz_it = std::find_if(backing_array_.begin(), backing_array_.end(), [] (word_type w) { return w != 0; });
    if (nz_it == backing_array_.end())
        return {-1, -1};
    size_t bit_idx = std::countr_zero(*nz_it);
    size_t word_idx = std::distance(backing_array_.begin(), nz_it);
    return {word_idx, bit_idx};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS std::string
TEMPL_CLASS::to_hex_string() const
{
    std::stringstream ss;

    std::string out;
    for (ssize_t i = NUM_WORDS-1; i >= 0; i--)
    {
        word_type w = backing_array_[i];
        for (ssize_t j = BITS_PER_WORD-4; j >= 0; j -= 4)
        {
            word_type nibble = (w >> j) & 0xf;
            ss << std::hex << nibble;
        }
        ss << " ";

        // only update `w` if the this word is nonzero
        if (w)
            out = ss.str();
    }

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS constexpr bool
TEMPL_CLASS::operator==(const TEMPL_CLASS& other) const
{
    return std::equal(backing_array_.begin(), backing_array_.end(), other.backing_array_.begin());
}

TEMPL_PARAMS constexpr bool
TEMPL_CLASS::operator!=(const TEMPL_CLASS& other) const
{
    return !(*this == other);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#undef TEMPL_PARAMS
#undef TEMPL_CLASS
