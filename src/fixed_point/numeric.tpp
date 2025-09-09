/*
    author: Suhas Vittal
    date:   20 September 2025
*/

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W> constexpr BIGINT_TYPE<W> 
bigint_from_hex_string(std::string_view s)
{
    std::array<BIGINT_TYPE<W>::word_type, BIGINT_TYPE<W>::NUM_WORDS> word_array{};

    constexpr size_t NUM_NIBBLES_PER_WORD{BIGINT_TYPE<W>::BITS_PER_WORD / 4};

    size_t nibble_count{0};
    size_t word_idx{0};

    for (ssize_t i = s.size()-1; i >= 0; i--)
    {
        char c = s[i];

        uint8_t nibble;
        if (c >= '0' && c <= '9')
            nibble = c - '0';
        else if (c >= 'a' && c <= 'f')
            nibble = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            nibble = c - 'A' + 10;
        else
            throw std::runtime_error("Invalid character in hex string: " + std::string(1, c));
            
        word_array[word_idx] |= (nibble << (nibble_count * 4));
            
        nibble_count++;
        if (nibble_count == NUM_NIBBLES_PER_WORD)
        {
            nibble_count = 0;
            word_idx++;
        }
    }

    return BIGINT_TYPE<W>(word_array);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace bigint
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W> BIGINT_TYPE<W>
negate(BIGINT_TYPE<W> x)
{
    // 2's complement -- invert all bits and add one
    x.transform([] (auto w) { return ~w; });
    constexpr BIGINT_TYPE<W>::word_type ONE{1};
    return add(x, ONE);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////


template <size_t W> BIGINT_TYPE<W> 
add(BIGINT_TYPE<W> x, BIGINT_TYPE<W> y)
{
    BIGINT_TYPE<W> out{};

    bool carry{false};
    for (size_t i = 0; i < BIGINT_TYPE<W>::NUM_WORDS; i++)
    {
        BIGINT_TYPE<W>::word_type u = x.test_word(i),
                                  v = y.test_word(i);
        BIGINT_TYPE<W>::word_type s = u+v+carry;
        carry = s < u || s < v;
        out.set_word(i, s);
    }
    return out;
}

template <size_t W> BIGINT_TYPE<W>
sub(BIGINT_TYPE<W> x, BIGINT_TYPE<W> y)
{
    return add(x, negate(y));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W> BIGINT_TYPE<W>
mul(BIGINT_TYPE<W> x, BIGINT_TYPE<W> y)
{
    // karatsuba multiplication -- recursive algorithm:
    if constexpr (W == 1)
    {
        return BIGINT_TYPE<W>{x.test_word(0) * y.test_word(0)};
    }
    else
    {
        BIGINT_TYPE<W> out{};

        // split `x` and `y` into two halves
        const auto& x_words = x.get_words_ref();
        const auto& y_words = y.get_words_ref();

        BIGINT_TYPE<W/2> x_lwr(x_words.begin(), x_words.begin() + W/2),
                         x_upp(x_words.begin() + W/2, x_words.end()),
                         y_lwr(y_words.begin(), y_words.begin() + W/2),
                         y_upp(y_words.begin() + W/2, y_words.end());

        // do multiplication:
        BIGINT_TYPE<W> z3{mul(add(x_upp, x_lwr), add(y_upp, y_lwr))};
        BIGINT_TYPE<W> z0{mul(x_lwr, y_lwr)};
        BIGINT_TYPE<W> z2{mul(x_upp, y_upp)};
        BIGINT_TYPE<W> z1 = sub(z3, sub(z2, z0));

        // merge results:
        out = add(add(z0, z1), z2);
        return out;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W> BIGINT_TYPE<W>
div(BIGINT_TYPE<W> x, BIGINT_TYPE<W> y)
{
    BIGINT_TYPE<W> quo{},
                    rem{};

    auto geq = [](BIGINT_TYPE<W> x, BIGINT_TYPE<W> y)
                {
                    for (ssize_t i = BIGINT_TYPE<W>::NUM_WORDS-1; i >= 0; i--)
                    {
                        if (x.test_word(i) > y.test_word(i))
                            return true;
                        if (x.test_word(i) < y.test_word(i))
                            return false;
                    }
                    return true;
                };
    
    for (ssize_t i = W-1; i >= 0; i--)
    {
        // set the least significant word of `rem` to `x[i]`
        rem.lshft(1);
        rem.set_bit(0, x.test_bit(i));

        if (geq(rem, y))
        {
            rem = sub(rem, y);
            quo.set_bit(i, true);
        }
    }

    return {quo, rem};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace bigint

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////