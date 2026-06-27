/*
 *  author: Suhas Vittal
 *  date:   5 January 2026
 * */

#include "compiler/program/value_info.h"

namespace compiler
{
namespace prog
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

/*
 * These are indices into the `Expression::generic_value_type` variant.
 * */
constexpr size_t INT_IDX{0},
                 FLOAT_IDX{1},
                 IDENT_IDX{2},
                 EXPR_IDX{3};

}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ValueInfo::ValueInfo(const generic_value_type& value)
{
    state = State::DEFAULT;
    if (value.index() == INT_IDX)
    {
        int64_t x = std::get<INT_IDX>(value);

        // check if x is a power of two
        bool is_power_of_two = (x & (x-1)) == 0;
        if (is_power_of_two)
        {
            // get logarithm using ffsll
            ssize_t log2 = ffsll(*(long long*)&x) - 1;
            power_of_two_exponent = log2;
            state = State::POWER_OF_TWO_IS_VALID;
        }
        else
        {
            state = State::IS_INTEGRAL;
        }
        
        // set the integer value regardless
        integral_value = x;

        // regardless of whether it is a power of two, we must update `floating_point`
        floating_point = static_cast<double>(x);
    }
    else if (value.index() == FLOAT_IDX)
    {
        floating_point = std::get<FLOAT_IDX>(value);
    }
    else if (value.index() == IDENT_IDX)
    {
        std::string ident = std::get<IDENT_IDX>(value);
        if (ident == "pi" || ident == "PI")
        {
            floating_point = M_PI;
            fixed_point.set(fpa_type::NUM_BITS - 1, true);
            state = State::CAN_USE_FIXED_POINT;
        }
        else if (ident == "e" || ident == "E")
        {
            floating_point = M_E;
        }
        else if (ident.find("fpa") != std::string::npos)
        {
            constexpr size_t NUM_BITS_PER_NIBBLE = fpa_type::BITS_PER_WORD/4;

            // create hex string:
            size_t width_start_idx = ident.find("fpa") + 3;
            size_t hex_ident_start_idx = ident.find("0x");
            size_t hex_start_idx = hex_ident_start_idx + 2;
            std::array<fpa_type::word_type, fpa_type::NUM_WORDS> words{};
            int nibble_count{NUM_BITS_PER_NIBBLE-1};
            int word_idx{fpa_type::NUM_WORDS-1};

            for (int i = hex_start_idx; i < ident.size(); i++)
            {
                char c = ident[i];
                fpa_type::word_type value{0};
                if (c >= '0' && c <= '9')
                    value = c - '0';
                else if (c >= 'a' && c <= 'f')
                    value = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    value = c - 'A' + 10;
                else
                    throw std::runtime_error("Unknown character `" + std::string{c} + "` found in expression: " + ident);

                words[word_idx] |= value << (nibble_count*4);
                nibble_count--;
                if (nibble_count < 0)
                {
                    nibble_count = NUM_BITS_PER_NIBBLE-1;
                    word_idx--;
                }
            }

            fixed_point = fpa_type(words);
            state = State::CAN_USE_FIXED_POINT;
        }
        else
        {
            throw std::runtime_error("Unknown identifier found in expression: " + ident);
        }
    }
    else if (value.index() == EXPR_IDX)
    {
        ValueInfo v = evaluate_expression(*std::get<EXPR_IDX>(value));
        *this = v;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ValueInfo
ValueInfo::init_as_one()
{
    ValueInfo v;

    v.power_of_two_exponent = 0;
    v.integral_value = 1;
    v.floating_point = 1.0;
    v.state = State::ONE;

    return v;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ValueInfo::fpa_type
ValueInfo::readout_fixed_point_angle() const
{
    return can_use_fixed_point() ? fixed_point
                                 : convert_float_to_fpa<fpa_type::NUM_BITS>(floating_point);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ValueInfo&
ValueInfo::operator+=(ValueInfo v)
{
    if (state == State::ZERO)
    {
        *this = v;
    }
    else if (v.state != State::ZERO)
    {
        if (can_use_fixed_point() && v.can_use_fixed_point())
            fpa::add_inplace(fixed_point, v.fixed_point);
        else
            state = State::DEFAULT;

        // always need to update floating point:
        floating_point += v.floating_point;
    }

    return *this;
}

ValueInfo&
ValueInfo::operator-=(ValueInfo v)
{
    if (state == State::ZERO)
    {
        *this = v.negated();
    }
    else if (v.state != State::ZERO)
    {
        if (can_use_fixed_point() && v.can_use_fixed_point())
            fpa::sub_inplace(fixed_point, v.fixed_point);
        else
            state = State::DEFAULT;

        floating_point -= v.floating_point;
    }
    return *this;
}

ValueInfo&
ValueInfo::operator*=(ValueInfo v)
{
    if (state == State::ZERO || v.state == State::ZERO)
    {
        *this = ValueInfo{};
        return *this;
    }

    if (can_use_fixed_point() && v.is_power_of_two())  // just a bitshift for `*this`
    {
        fixed_point.lshft(v.power_of_two_exponent);
    }
    else if (can_use_fixed_point() && v.is_integral())
    {
        fpa::scalar_mul_inplace(fixed_point, v.integral_value);
    }
    else if (is_power_of_two() && v.can_use_fixed_point())
    {
        fixed_point = v.fixed_point;
        fixed_point.lshft(power_of_two_exponent);
        state = State::CAN_USE_FIXED_POINT;
    }
    else if (is_power_of_two() && v.is_power_of_two())
    {
        power_of_two_exponent += v.power_of_two_exponent;
    }
    else
    {
        state = State::DEFAULT;
    }

    is_negated ^= v.is_negated;

    floating_point *= v.floating_point;
    return *this;
}

ValueInfo&
ValueInfo::operator/=(ValueInfo v)
{
    if (state == State::ZERO)
        return *this;
    if (v.state == State::ONE)
        return *this;
    if (v.state == State::ZERO)
        throw std::runtime_error("Division by zero");

    if (can_use_fixed_point() && v.is_power_of_two())
        fixed_point.rshft(v.power_of_two_exponent);
    else if (is_power_of_two() && v.is_power_of_two())
        power_of_two_exponent -= v.power_of_two_exponent;
    else
        state = State::DEFAULT;

    is_negated ^= v.is_negated;

    floating_point /= v.floating_point;
    return *this;
}

ValueInfo&
ValueInfo::operator^=(ValueInfo v)
{
    if (state == State::ZERO)
        return *this;

    if (v.is_power_of_two() && v.power_of_two_exponent == 0)
        return *this;

    if (v.state == State::ZERO)
    {
        generic_value_type x;
        x.emplace<INT_IDX>(1);
        *this = ValueInfo(x);
    }
    else
    {
        if (is_power_of_two() && v.is_power_of_two())
            power_of_two_exponent *= (1LL << v.power_of_two_exponent);
        else if (is_power_of_two() && v.is_integral())
            power_of_two_exponent *= v.integral_value;
        else
            state = State::DEFAULT;
    }

    floating_point = std::pow(floating_point, v.floating_point);
    return *this;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ValueInfo
ValueInfo::negated() const
{
    ValueInfo v = *this;
    v.is_negated = !v.is_negated;
    return v;
}

void
ValueInfo::consume_negated()
{
    if (is_negated)
    {
        is_negated = false;
        integral_value = -integral_value;
        floating_point = -floating_point;
        fpa::negate_inplace(fixed_point);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ValueInfo::can_use_fixed_point() const
{
    return state == State::ZERO || state == State::CAN_USE_FIXED_POINT;
}

bool
ValueInfo::is_power_of_two() const
{
    return state == State::ONE || state == State::POWER_OF_TWO_IS_VALID;
}

bool
ValueInfo::is_integral() const
{
    return is_power_of_two() || state == State::IS_INTEGRAL;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::string
ValueInfo::to_string() const
{
    std::stringstream ss;

    constexpr std::string_view STATE_NAMES[] = 
    {
        "DEFAULT",
        "ZERO",
        "ONE",
        "IS_INTEGRAL",
        "CAN_USE_FIXED_POINT",
        "POWER_OF_TWO_IS_VALID"
    };

    if (is_negated)
        ss << "-";

    if (state == State::POWER_OF_TWO_IS_VALID)
    {
        if (power_of_two_exponent <= 13)
            ss << (1L << power_of_two_exponent);
        else
            ss << "2^" << power_of_two_exponent;
    }
    else if (state == State::IS_INTEGRAL)
    {
        ss << integral_value;
    }
    else if (state == State::CAN_USE_FIXED_POINT)
    {
        ss << fpa::to_string(fixed_point);
    }
    else if (state == State::DEFAULT)
    {
        ss << floating_point;
    }
    else if (state == State::ONE)
    {
        ss << "1";
    }
    else
    {
        ss << "0";
    }

    ss << " (s" << STATE_NAMES[static_cast<size_t>(state)] << ")";

    return ss.str();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ValueInfo
operator+(ValueInfo a, ValueInfo b)
{
    return a += b;
}

ValueInfo
operator-(ValueInfo a, ValueInfo b)
{
    return a -= b;
}

ValueInfo
operator*(ValueInfo a, ValueInfo b)
{
    return a *= b;
}


ValueInfo
operator/(ValueInfo a, ValueInfo b)
{
    return a /= b;
}

ValueInfo
operator^(ValueInfo a, ValueInfo b)
{
    return a ^= b;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace prog
}   // namespace compiler
