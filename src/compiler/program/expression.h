/*
    author: Suhas Vittal
    date:   23 August 2025
*/

#ifndef COMPILER_PROGRAM_EXPRESSION_h
#define COMPILER_PROGRAM_EXPRESSION_h

#include "compiler/program.h"

#include <cstdint>

namespace prog
{
namespace expr
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct VALUE_INFO
{
    using fpa_type = PROGRAM_INFO::fpa_type;
    
    enum class STATE 
    {
        DEFAULT,
        ZERO,
        ONE,
        IS_INTEGRAL,
        CAN_USE_FIXED_POINT,
        POWER_OF_TWO_IS_VALID
    };

    bool      is_negated{false};
    ssize_t   power_of_two_exponent{0};
    fpa_type  fixed_point{};
    int64_t   integral_value{0};
    double    floating_point{0.0};
    STATE     state{STATE::ZERO};

    VALUE_INFO() =default;
    VALUE_INFO(const VALUE_INFO&) =default;

    VALUE_INFO(const EXPRESSION::generic_value_type&);

    static VALUE_INFO init_as_one();

    fpa_type readout_fixed_point_angle() const;

    VALUE_INFO& operator+=(VALUE_INFO);
    VALUE_INFO& operator-=(VALUE_INFO);
    VALUE_INFO& operator*=(VALUE_INFO);
    VALUE_INFO& operator/=(VALUE_INFO);
    VALUE_INFO& operator^=(VALUE_INFO);

    VALUE_INFO negated() const;
    void consume_negated();

    bool can_use_fixed_point() const;
    bool is_power_of_two() const;
    bool is_integral() const;

    std::string to_string() const;
};

VALUE_INFO operator+(VALUE_INFO, VALUE_INFO);
VALUE_INFO operator-(VALUE_INFO, VALUE_INFO);
VALUE_INFO operator*(VALUE_INFO, VALUE_INFO);
VALUE_INFO operator/(VALUE_INFO, VALUE_INFO);
VALUE_INFO operator^(VALUE_INFO, VALUE_INFO);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

VALUE_INFO evaluate_expression(const prog::EXPRESSION&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace expr
}   // namespace prog

#endif  // PROGRAM_EXPRESSION_h