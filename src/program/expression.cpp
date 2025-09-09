/*
    author: Suhas Vittal
    date:   23 August 2025
    */

#include "expression.h"

#include <iostream>

#include <strings.h>

namespace prog
{
namespace expr
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr size_t INT_IDX{0},
                FLOAT_IDX{1},
                IDENT_IDX{2},
                EXPR_IDX{3};

VALUE_INFO::VALUE_INFO(const EXPRESSION::generic_value_type& value)
{
    state = STATE::DEFAULT;
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
            state = STATE::POWER_OF_TWO_IS_VALID;
        }
        else
        {
            state = STATE::IS_INTEGRAL;
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
            state = STATE::CAN_USE_FIXED_POINT;
        }
        else if (ident == "e" || ident == "E")
        {
            floating_point = M_E;
        }
        else if (ident.find("fpa") != std::string::npos)
        {
            // create hex string:
            size_t width_start_idx = ident.find("fpa") + 3;
            size_t hex_ident_start_idx = ident.find("0x");
            size_t num_bits = std::stoi(ident.substr(width_start_idx, hex_ident_start_idx - width_start_idx));

            size_t hex_start_idx = hex_ident_start_idx + 2;

            std::array<fpa_type::word_type, fpa_type::NUM_WORDS> words{};
            size_t nibble_count{0};
            size_t word_idx{0};
            for (ssize_t i = ident.size()-1; i >= hex_start_idx; i--)
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
                nibble_count++;

                if (nibble_count == fpa_type::BITS_PER_WORD/4)
                {
                    nibble_count = 0;
                    word_idx++;
                }
            }

            fixed_point = fpa_type(words);
            fixed_point.lshft(fpa_type::NUM_BITS - num_bits);

            state = STATE::CAN_USE_FIXED_POINT;
        }
        else
        {
            throw std::runtime_error("Unknown identifier found in expression: " + ident);
        }
    }
    else if (value.index() == EXPR_IDX)
    {
        VALUE_INFO v = evaluate_expression(*std::get<EXPR_IDX>(value));
        *this = v;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

VALUE_INFO
VALUE_INFO::init_as_one()
{
    VALUE_INFO v;

    v.power_of_two_exponent = 0;
    v.integral_value = 1;
    v.floating_point = 1.0;
    v.state = STATE::ONE;

    return v;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

VALUE_INFO::fpa_type
VALUE_INFO::readout_fixed_point_angle() const
{
    return can_use_fixed_point() ? fixed_point
                                 : convert_float_to_fpa<fpa_type::NUM_BITS>(floating_point);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

VALUE_INFO&
VALUE_INFO::operator+=(VALUE_INFO v)
{
    if (state == STATE::ZERO)
    {
        *this = v;
    }
    else if (v.state != STATE::ZERO)
    {
        if (can_use_fixed_point() && v.can_use_fixed_point())
            fpa::add_inplace(fixed_point, v.fixed_point);
        else
            state = STATE::DEFAULT;

        // always need to update floating point:
        floating_point += v.floating_point;
    }

    return *this;
}

VALUE_INFO&
VALUE_INFO::operator-=(VALUE_INFO v)
{
    if (state == STATE::ZERO)
    {
        *this = v.negated();
    }
    else if (v.state != STATE::ZERO)
    {
        if (can_use_fixed_point() && v.can_use_fixed_point())
            fpa::sub_inplace(fixed_point, v.fixed_point);
        else
            state = STATE::DEFAULT;

        floating_point -= v.floating_point;
    }
    return *this;
}

VALUE_INFO&
VALUE_INFO::operator*=(VALUE_INFO v)
{
    if (state == STATE::ZERO || v.state == STATE::ZERO)
    {
        *this = VALUE_INFO{};
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
        state = STATE::CAN_USE_FIXED_POINT;
    }
    else if (is_power_of_two() && v.is_power_of_two())
    {
        power_of_two_exponent += v.power_of_two_exponent;
    }
    else
    {
        state = STATE::DEFAULT;
    }

    is_negated ^= v.is_negated;

    floating_point *= v.floating_point;
    return *this;
}

VALUE_INFO&
VALUE_INFO::operator/=(VALUE_INFO v)
{
    if (state == STATE::ZERO)
        return *this;
    if (v.state == STATE::ONE)
        return *this;
    if (v.state == STATE::ZERO)
        throw std::runtime_error("Division by zero");

    if (can_use_fixed_point() && v.is_power_of_two())
        fixed_point.rshft(v.power_of_two_exponent);
    else if (is_power_of_two() && v.is_power_of_two())
        power_of_two_exponent -= v.power_of_two_exponent;
    else
        state = STATE::DEFAULT;

    is_negated ^= v.is_negated;

    floating_point /= v.floating_point;
    return *this;
}

VALUE_INFO&
VALUE_INFO::operator^=(VALUE_INFO v)
{
    if (state == STATE::ZERO)
        return *this;

    if (v.is_power_of_two() && v.power_of_two_exponent == 0)
        return *this;

    if (v.state == STATE::ZERO)
    {
        EXPRESSION::generic_value_type x;
        x.emplace<INT_IDX>(1);
        *this = VALUE_INFO(x);
    }
    else
    {
        if (is_power_of_two() && v.is_power_of_two())
            power_of_two_exponent *= (1LL << v.power_of_two_exponent);
        else if (is_power_of_two() && v.is_integral())
            power_of_two_exponent *= v.integral_value;
        else
            state = STATE::DEFAULT;
    }

    floating_point = std::pow(floating_point, v.floating_point);
    return *this;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

VALUE_INFO
VALUE_INFO::negated() const
{
    VALUE_INFO v = *this;
    v.is_negated = !v.is_negated;
    return v;
}

void
VALUE_INFO::consume_negated()
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
VALUE_INFO::can_use_fixed_point() const
{
    return state == STATE::ZERO || state == STATE::CAN_USE_FIXED_POINT;
}

bool
VALUE_INFO::is_power_of_two() const
{
    return state == STATE::ONE || state == STATE::POWER_OF_TWO_IS_VALID;
}

bool
VALUE_INFO::is_integral() const
{
    return is_power_of_two() || state == STATE::IS_INTEGRAL;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::string
VALUE_INFO::to_string() const
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

    if (state == STATE::POWER_OF_TWO_IS_VALID)
    {
        if (power_of_two_exponent <= 13)
            ss << (1L << power_of_two_exponent);
        else
            ss << "2^" << power_of_two_exponent;
    }
    else if (state == STATE::IS_INTEGRAL)
    {
        ss << integral_value;
    }
    else if (state == STATE::CAN_USE_FIXED_POINT)
    {
        ss << fpa::to_string(fixed_point);
    }
    else if (state == STATE::DEFAULT)
    {
        ss << floating_point;
    }
    else if (state == STATE::ONE)
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

VALUE_INFO
operator+(VALUE_INFO a, VALUE_INFO b)
{
    return a += b;
}

VALUE_INFO
operator-(VALUE_INFO a, VALUE_INFO b)
{
    return a -= b;
}

VALUE_INFO
operator*(VALUE_INFO a, VALUE_INFO b)
{
    return a *= b;
}


VALUE_INFO
operator/(VALUE_INFO a, VALUE_INFO b)
{
    return a /= b;
}

VALUE_INFO
operator^(VALUE_INFO a, VALUE_INFO b)
{
    return a ^= b;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////


std::string
_op_to_string(EXPRESSION::OPERATOR op)
{
    switch (op)
    {
        case EXPRESSION::OPERATOR::ADD:
            return "+";
        case EXPRESSION::OPERATOR::SUBTRACT:
            return "-";
        case EXPRESSION::OPERATOR::MULTIPLY:
            return "*";
        default:
            return "/";
    }
}

VALUE_INFO
_evaluate_expval(const EXPRESSION::exponential_value_type& expval)
{
    VALUE_INFO result = VALUE_INFO::init_as_one();

    const auto& [powseq, is_negative] = expval;
    // evaluate from right to left:
    for (auto it = powseq.rbegin(); it != powseq.rend(); it++)
    {
        VALUE_INFO v{*it};

#if defined(EXPRESSION_EVAL_DEBUG)
        std::cout << "exp_op: " << v.to_string() << " ** " << result.to_string() << "\n";
#endif
        
        result = v ^ result;

#if defined(EXPRESSION_EVAL_DEBUG)
        std::cout << "expval: " << result.to_string() << "\n";
#endif
    }

    result.is_negated ^= is_negative;
    return result;
}


VALUE_INFO
_evaluate_term(const EXPRESSION::term_type& term)
{
    VALUE_INFO result = VALUE_INFO::init_as_one();
    for (const auto& [expval, op] : term)
    {
        VALUE_INFO v;
        v = _evaluate_expval(expval);
    
#if defined(EXPRESSION_EVAL_DEBUG)
        std::cout << "term_op: " << result.to_string() << " " << _op_to_string(op) << " " << v.to_string() << "\n";
#endif

        if (op == EXPRESSION::OPERATOR::MULTIPLY)
            result *= v;
        else if (op == EXPRESSION::OPERATOR::DIVIDE)
            result /= v;
        else
            throw std::runtime_error("unexpected operator found in term: " + _op_to_string(op));

#if defined(EXPRESSION_EVAL_DEBUG)
        std::cout << "term: " << result.to_string() << "\n";
#endif
    }

    return result;
}

VALUE_INFO
evaluate_expression(const EXPRESSION& expr)
{
    VALUE_INFO result{};

    for (const auto& [term, op] : expr.termseq)
    {
        VALUE_INFO v = _evaluate_term(term);
        
        // here we should start consuming `is_negated` since all factors have been evaluated
        v.consume_negated();
        
#if defined(EXPRESSION_EVAL_DEBUG)
        std::cout << "expr_op: " << result.to_string() << " " << _op_to_string(op) << " " << v.to_string() << "\n";
#endif

        if (op == EXPRESSION::OPERATOR::ADD)
            result += v;
        else if (op == EXPRESSION::OPERATOR::SUBTRACT)
            result -= v;
        else
            throw std::runtime_error("unexpected operator found in expression: " + _op_to_string(op));

#if defined(EXPRESSION_EVAL_DEBUG)
        std::cout << "expr:  " << result.to_string() << "\n"; 
#endif
    }
    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace expr
}   // namespace prog