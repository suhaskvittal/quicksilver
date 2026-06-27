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

namespace compiler
{
namespace prog
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// Forward declarations
struct Expression;
struct ValueInfo;

// Operator enum (moved from Expression)
enum class Operator { ADD, SUBTRACT, MULTIPLY, DIVIDE };

// Base types
using expr_ptr = std::shared_ptr<Expression>;
using generic_value_type = std::variant<int64_t, double, std::string, expr_ptr>;

// Expression component types (flattened from Expression)
struct ExponentialValue
{
    std::vector<generic_value_type> power_sequence;
    bool is_negated{false};
};

struct Factor
{
    ExponentialValue exponential_value;
    Operator operator_with_previous;
};

struct Term
{
    std::vector<Factor> factors;
};

struct TermEntry
{
    Term term;
    Operator operator_with_previous;
};

struct Expression
{
    std::vector<TermEntry> terms;

    std::string to_string() const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Converts an `Expression` (symbolic) to an actual value
 * represented by either a `double` or fixed point type.
 * */
ValueInfo evaluate_expression(const Expression&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace prog
}   // namespace compiler

#endif  // COMPILER_PROGRAM_EXPRESSION_h
