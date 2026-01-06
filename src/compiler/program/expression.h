/*
    author: Suhas Vittal
    date:   23 August 2025
*/

#ifndef COMPILER_PROGRAM_EXPRESSION_h
#define COMPILER_PROGRAM_EXPRESSION_h

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace prog
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// Forward declarations
struct EXPRESSION;
struct VALUE_INFO;

// Operator enum (moved from EXPRESSION)
enum class OPERATOR { ADD, SUBTRACT, MULTIPLY, DIVIDE };

// Base types
using expr_ptr = std::shared_ptr<EXPRESSION>;
using generic_value_type = std::variant<int64_t, double, std::string, expr_ptr>;

// Expression component types (flattened from EXPRESSION)
struct EXPONENTIAL_VALUE
{
    std::vector<generic_value_type> power_sequence;
    bool is_negated{false};
};

struct FACTOR
{
    EXPONENTIAL_VALUE exponential_value;
    OPERATOR operator_with_previous;
};

struct TERM
{
    std::vector<FACTOR> factors;
};

struct TERM_ENTRY
{
    TERM term;
    OPERATOR operator_with_previous;
};

struct EXPRESSION
{
    std::vector<TERM_ENTRY> terms;

    std::string to_string() const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Converts an `EXPRESSION` (symbolic) to an actual value
 * represented by either a `double` or fixed point type.
 * */
VALUE_INFO evaluate_expression(const EXPRESSION&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace prog

#endif  // COMPILER_PROGRAM_EXPRESSION_h
