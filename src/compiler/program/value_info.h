/*
 *  author: Suhas Vittal
 *  date:   5 January 2026
 * */

#ifndef COMPILER_PROGRAM_VALUE_INFO_h
#define COMPILER_PROGRAM_VALUE_INFO_h

#include "compiler/program/expression.h"
#include "compiler/program.h"

#include <cstdint>

namespace compiler
{
namespace prog
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct ValueInfo
{
    using fpa_type = ProgramInfo::fpa_type;
    
    enum class State 
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
    State     state{State::ZERO};

    ValueInfo() =default;
    ValueInfo(const ValueInfo&) =default;
    ValueInfo(const generic_value_type&);

    static ValueInfo init_as_one();

    fpa_type readout_fixed_point_angle() const;

    ValueInfo& operator+=(ValueInfo);
    ValueInfo& operator-=(ValueInfo);
    ValueInfo& operator*=(ValueInfo);
    ValueInfo& operator/=(ValueInfo);
    ValueInfo& operator^=(ValueInfo);

    ValueInfo negated() const;
    void consume_negated();

    bool can_use_fixed_point() const;
    bool is_power_of_two() const;
    bool is_integral() const;

    std::string to_string() const;
};

ValueInfo operator+(ValueInfo, ValueInfo);
ValueInfo operator-(ValueInfo, ValueInfo);
ValueInfo operator*(ValueInfo, ValueInfo);
ValueInfo operator/(ValueInfo, ValueInfo);
ValueInfo operator^(ValueInfo, ValueInfo);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace prog
} // namespace compiler

#endif  // COMPILER_PROGRAM_VALUE_INFO_h
