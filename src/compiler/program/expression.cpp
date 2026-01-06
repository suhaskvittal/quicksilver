/*
    author: Suhas Vittal
    date:   23 August 2025
    */

#include "compiler/program/expression.h"
#include "compiler/program/value_info.h"

#include <iostream>

#include <strings.h>

//#define EXPRESSION_EVAL_DEBUG

namespace prog
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Helper functions
 * */

namespace
{

std::string _generic_value_to_string(generic_value_type);

std::string _op_to_string(OPERATOR);
VALUE_INFO  _evaluate_expval(const EXPONENTIAL_VALUE&);
VALUE_INFO  _evaluate_term(const TERM&);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::string
EXPRESSION::to_string() const
{
    std::stringstream ss;
    for (size_t i = 0; i < terms.size(); ++i)
    {
        // print out term operator:
        const auto& entry = terms[i];
        const auto& op = entry.operator_with_previous;
        if (i > 0)
            ss << (op == OPERATOR::ADD ? " + " : " - ");

        ss << "(";
        for (size_t j = 0; j < entry.term.factors.size(); ++j)
        {
            const auto& factor = entry.term.factors[j];
            const auto& op2 = factor.operator_with_previous;
            if (j > 0)
                ss << (op2 == OPERATOR::MULTIPLY ? " * " : "/ ");

            if (factor.exponential_value.is_negated)
                ss << "-";

            ss << "(";
            for (size_t k = 0; k < factor.exponential_value.power_sequence.size(); ++k)
            {
                if (k > 0)
                    ss << "^";
                ss << _generic_value_to_string(factor.exponential_value.power_sequence[k]);
            }
            ss << ")";
        }
        ss << ")";
    }

    return ss.str();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

VALUE_INFO
evaluate_expression(const EXPRESSION& expr)
{
    VALUE_INFO result{};
    for (const auto& entry : expr.terms)
    {
        VALUE_INFO v = _evaluate_term(entry.term);

        // here we should start consuming `is_negated` since all factors have been evaluated
        v.consume_negated();
        if (entry.operator_with_previous == OPERATOR::ADD)
            result += v;
        else if (entry.operator_with_previous == OPERATOR::SUBTRACT)
            result -= v;
        else
            std::cerr << "evaluate_expression: unexpected operator " << _op_to_string(entry.operator_with_previous) << _die{};
    }
    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/* HELPER FUNCTIONS START HERE */

namespace
{

std::string
_generic_value_to_string(generic_value_type val)
{
    if (std::holds_alternative<int64_t>(val))
        return std::to_string(std::get<int64_t>(val));
    else if (std::holds_alternative<double>(val))
        return std::to_string(std::get<double>(val));
    else if (std::holds_alternative<std::string>(val))
        return std::get<std::string>(val);
    else
        return std::get<expr_ptr>(val)->to_string();
}

std::string
_op_to_string(OPERATOR op)
{
    switch (op)
    {
        case OPERATOR::ADD:
            return "+";
        case OPERATOR::SUBTRACT:
            return "-";
        case OPERATOR::MULTIPLY:
            return "*";
        default:
            return "/";
    }
}

VALUE_INFO
_evaluate_term(const TERM& term)
{
    VALUE_INFO result = VALUE_INFO::init_as_one();
    for (const auto& factor : term.factors)
    {
        VALUE_INFO v;
        v = _evaluate_expval(factor.exponential_value);
        if (factor.operator_with_previous == OPERATOR::MULTIPLY)
            result *= v;
        else if (factor.operator_with_previous == OPERATOR::DIVIDE)
            result /= v;
        else
            std::cerr << "_evaluate_term: unexpected operator " << _op_to_string(factor.operator_with_previous) << _die{};
    }
    return result;
}

VALUE_INFO
_evaluate_expval(const EXPONENTIAL_VALUE& expval)
{
    VALUE_INFO result = VALUE_INFO::init_as_one();
    const auto& powseq = expval.power_sequence;

    // evaluate from right to left:
    for (auto it = powseq.rbegin(); it != powseq.rend(); it++)
        result = VALUE_INFO{*it} ^ result;
    result.is_negated ^= expval.is_negated;
    return result;
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace prog
