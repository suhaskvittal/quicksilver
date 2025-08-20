/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#include <cmath>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W> fpa_type<W> 
convert_float_to_fpa(double x)
{
    fpa_type<W> out{};
    auto fpeq = [tol] (double x, double y) { return x > y-tol && x < y+tol; };
    
    size_t idx{fpa_type<W>::NUM_BITS-1};
    double m{M_PI};
    while (x > 1e-18)
    {
        bool b = (x > m || fpeq(x,m));
        out.set(idx, b);
        x -= b ? m : 0.0;
        m *= 0.5;
        idx--;
    }

    return out;
}

template <size_t W> double
convert_fpa_to_float(const fpa_type<W>& x)
{
    const double tol{1e-18};

    double out{0.0};
    double m{M_PI};
    for (size_t i = 0; i < fpa_type<W>::NUM_BITS; i++)
    {
        out += x.test(i) ? m : 0.0;
        m *= 0.5;
    }
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace fpa
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W> void
negate_inplace(fpa_type<W>& x)
{
    using typename fpa_type<W>::word_type;
    /*
        Examples with a four-bit FPA:
            negation of PI (1000) is just PI (1000)
            negation of PI/2 (0100) is -PI/2 = 3*PI/2 (1100)
            negation of PI/4 (0010) is -PI/4 = 7*PI/4 (1110)
            negation of 3PI/4 (0110) is -3PI/4 = 5I/4 (1010)
            etc.
        Algorithm:
            find msb.
            flip all bits after the msb.
    */

    auto [msb_word_idx, msb_bit_idx] = x.msb();
    
    // for the msb word, we need to flip all bits after the msb bit.
    word_type w = x.test_word(msb_word_idx);
    word_type mask = (1L << (fpa_type<W>::BITS_PER_WORD-msb_bit_idx-1)) - 1;
    w ^= mask << (msb_bit_idx+1);
    x.set_word(msb_word_idx, w);

    // now flip all bits in the remaining words above the msb word
    for (size_t i = msb_word_idx+1; i < fpa_type<W>::NUM_WORDS; i++)
        x.set_word(i, ~x.test_word(i));
}

template <size_t W> void
add_inplace(fpa_type<W>& x, fpa_type<W> y)
{
    using typename fpa_type<W>::word_type;
    /*
        Examples with a four-bit FPA:
            PI (1000) + PI/2 (0100) = 3PI/2 (1100)
            PI (1000) + PI/4 (0010) = 5PI/4 (1010)
            PI (1000) + 3PI/4 (0110) = 7PI/4 (1110)
        so it is just simple addition. We need to handle the carryout.
    */
    word_type cout{0};
    for (size_t i = 0; i < fpa_type<W>::NUM_WORDS; i++)
    {
        word_type u = x.test_word(i),
                  v = y.test_word(i);
        word_type s = u+v+cout;
        cout = static_cast<word_type>(s < u || s < v);
        x.set_word(i, s);
    }
}

template <size_t W> void
sub_inplace(fpa_type<W>& x, fpa_type<W> y)
{
    negate_inplace(y);
    add_inplace(x, y);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W> fpa_type<W>
negate(fpa_type<W> x)
{
    negate_inplace(x);
    return x;
}

template <size_t W> fpa_type<W>
add(fpa_type<W> x, fpa_type<W> y)
{
    add_inplace(x, y);
    return x;
}

template <size_t W> fpa_type<W>
sub(fpa_type<W> x, fpa_type<W> y)
{
    sub_inplace(x, y);
    return x;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace fpa

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////