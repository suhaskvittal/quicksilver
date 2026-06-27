/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#include <bit>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W> constexpr FPAType<W>
convert_float_to_fpa(double x, double tol)
{
    // bound `x` to the range [0, 2*M_PI)
    while (x > 2*M_PI)
        x -= 2*M_PI;
    while (x < 0)
        x += 2*M_PI;

    FPAType<W> out{};
    auto fpeq = [tol] (double x, double y) { return x > y-tol && x < y+tol; };

    size_t idx{FPAType<W>::NUM_BITS-1};
    double m{M_PI};
    while (x > tol)
    {
        bool b = (x > m || fpeq(x,m));
        out.set(idx, b);
        x -= b ? m : 0.0;
        m *= 0.5;
        idx--;
    }

    return out;
}

template <size_t W> constexpr double
convert_fpa_to_float(const FPAType<W>& x)
{
    double out{0.0};
    double m{M_PI};
    for (ssize_t i = FPAType<W>::NUM_BITS-1; i >= 0; i--)
    {
        out += x.test(i) ? m : 0.0;
        m *= 0.5;
    }
    if (out > M_PI)
        out -= 2*M_PI;
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace fpa
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W> constexpr void
negate_inplace(FPAType<W>& x)
{
    using word_type = typename FPAType<W>::word_type;
    /*
        Examples with a four-bit FPA:
            negation of PI (1000) is just PI (1000)
            negation of PI/2 (0100) is -PI/2 = 3*PI/2 (1100)
            negation of PI/4 (0010) is -PI/4 = 7*PI/4 (1110)
            negation of 3PI/4 (0110) is -3PI/4 = 5I/4 (1010)
            etc.
        Algorithm:
            find lsb.
            flip all bits after the lsb.
    */

    if (x.popcount() == 0)
        return;

    auto [word_idx, bit_idx] = x.lsb();

    // for the lsb word, we need to flip all bits after the lsb bit.
    if (bit_idx < FPAType<W>::BITS_PER_WORD-1)
    {
        word_type w = x.test_word(word_idx);
        size_t shift = FPAType<W>::BITS_PER_WORD-bit_idx-1;
        word_type mask = (word_type{1} << shift) - 1;
        w ^= (mask << (bit_idx+1));
        x.set_word(word_idx, w);
    }

    // now flip all bits in the remaining words above the lsb word
    for (size_t i = word_idx+1; i < FPAType<W>::NUM_WORDS; i++)
        x.set_word(i, ~x.test_word(i));
}

template <size_t W> constexpr void
add_inplace(FPAType<W>& x, FPAType<W> y)
{
    using word_type = typename FPAType<W>::word_type;
    /*
        Examples with a four-bit FPA:
            PI (1000) + PI/2 (0100) = 3PI/2 (1100)
            PI (1000) + PI/4 (0010) = 5PI/4 (1010)
            PI (1000) + 3PI/4 (0110) = 7PI/4 (1110)
        so it is just simple addition. We need to handle the carryout.
    */
    word_type cout{0};
    for (size_t i = 0; i < FPAType<W>::NUM_WORDS; i++)
    {
        word_type u = x.test_word(i),
                  v = y.test_word(i);
        word_type s = u+v+cout;
        cout = static_cast<word_type>(s < u || s < v);
        x.set_word(i, s);
    }
}

template <size_t W> constexpr void
sub_inplace(FPAType<W>& x, FPAType<W> y)
{
    negate_inplace(y);
    add_inplace(x, y);
}

template <size_t W> constexpr void
scalar_mul_inplace(FPAType<W>& x, int64_t y)
{
    if (y < 0) // transfer the negative to `x` and multiply by `-y`
    {
        negate_inplace(x);
        scalar_mul_inplace(x, -y);
        return;
    }

    // algorithm, mostly because I am too lazy to implement FFT:
    //  accumulate `x_base << i` into `x` for each set bit `i` of `y`.
    //  `x` is the accumulator, so it must start at zero (handles y == 0 too).
    FPAType<W> x_base{x};
    x = FPAType<W>{};
    while (y)
    {
        size_t lsb_bit = std::countr_zero(static_cast<uint64_t>(y));
        FPAType<W> tmp{x_base};
        tmp.lshft(lsb_bit);
        add_inplace(x, tmp);
        y &= ~(int64_t{1} << lsb_bit);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W> constexpr FPAType<W>
negate(FPAType<W> x)
{
    negate_inplace(x);
    return x;
}

template <size_t W> constexpr FPAType<W>
add(FPAType<W> x, FPAType<W> y)
{
    add_inplace(x, y);
    return x;
}

template <size_t W> constexpr FPAType<W>
sub(FPAType<W> x, FPAType<W> y)
{
    sub_inplace(x, y);
    return x;
}

template <size_t W> constexpr FPAType<W>
scalar_mul(FPAType<W> x, int64_t y)
{
    FPAType<W> out{x};
    scalar_mul_inplace(out, y);
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W> std::string
to_string(const FPAType<W>& x, StringFormat fmt)
{
    // number of tolerated bits before we return the floating point representation
    // we will use an expression as sums of pi if either `x` or `-x` has
    // `popcount <= MAX_POPCOUNT_BEFORE_FLOAT_CONV`
    constexpr size_t MAX_POPCOUNT_BEFORE_FLOAT_CONV{3};

    auto nx = negate(x);

    size_t cnt = x.popcount();
    size_t cnt_neg = nx.popcount();

    bool use_precise_format = (fmt == StringFormat::PRETTY && (cnt <= MAX_POPCOUNT_BEFORE_FLOAT_CONV || cnt_neg <= MAX_POPCOUNT_BEFORE_FLOAT_CONV))
                                || fmt == StringFormat::GRIDSYNTH
                                || ((fmt == StringFormat::FORCE_DECIMAL || fmt == StringFormat::GRIDSYNTH_CPP) && cnt == 1);

    std::stringstream ss;
    if (cnt == 0)
    {
        ss << "0";
    }
    else if (use_precise_format)
    {
        bool use_negative = cnt_neg < cnt;
        FPAType<W> y = use_negative ? negate(x) : x;
        bool first{true};
        for (size_t i = 0; i < FPAType<W>::NUM_BITS; i++)
        {
            if (y.test(i))
            {
                // add operand in front of term
                if (fmt == StringFormat::GRIDSYNTH)
                {
                    if (!first)
                        ss << " + ";
                }
                else
                {
                    if (!first)
                        ss << (use_negative ? " - " : " + ");
                    else if (use_negative)
                        ss << "-";
                }

                // need parentheses for gridsynth format:
                if (fmt == StringFormat::GRIDSYNTH)
                {
                    ss << "(";
                    if (use_negative)
                        ss << "-";
                }

                ss << "pi";
                if (i < FPAType<W>::NUM_BITS-1)
                {
                    size_t exp = FPAType<W>::NUM_BITS-i-1;
                    if (exp == 1)
                        ss << "/2";
                    else if (exp >= 2 && exp <= 13)
                        ss << "/" << (1L<<exp);
                    else
                        ss << "/2^" << exp;
                }

                if (fmt == StringFormat::GRIDSYNTH)
                    ss << ")";

                first = false;
            }
        }
    }
    else
    {
        ss << std::setprecision(5) << convert_fpa_to_float(x);
    }
    return ss.str();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace fpa

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
