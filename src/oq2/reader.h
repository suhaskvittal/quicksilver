/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#ifndef OQ2_READER_h
#define OQ2_READER_h

#include "fixed_point/angle.h"

#include <cstdint>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace oq2
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct TOKEN
{
    enum class TYPE
    {
        // keywords:
        OPENQASM,           // "OPENQASM"
        INCLUDE,            // "include"
        REGISTER,           // qreg or creg
        GATE,               // this is literally the string "gate"
        OPAQUE,             // "opaque"
        IF,
        SYMBOLIC,           // i.e., pi, e

        // identifier and literals:
        IDENTIFIER,         // i.e., qubits, q, qr, etc.
        STRING_LITERAL,     // i.e., "file.inc"
        INTEGER_LITERAL,    // i.e., 1, 2, 3, etc.
        FLOAT_LITERAL,      // i.e., 1.0, 2.0, 3.0, etc.
        
        // delimiters:
        LPAREN, RPAREN,
        LBRACKET, RBRACKET,
        LBRACE, RBRACE,
        COMMA,
        SEMICOLON,           

        // operators:
        COMPARISON_OPERATOR, // i.e., ==, !=, <, >, <=, >=
        ARROW,               // ->
        ARITHMETIC_OPERATOR, // i.e., +, -, *, /, **, ^
        PLUS,                
        MINUS,               
        MULTIPLY,            
        DIVIDE,              
        POWER,               // ** or ^
        INVALID,
        
        // ignore:
        WHITESPACE,
        COMMENT,            // C-like comments

        // used by `EAT_LINE_TO_END` state only
        LINE_CONTENT,
        EOL,

        // used by `VERSION_STRING` state only
        VERSION_STRING      // i.e., 2.0 or 2.0.1
    };

    TYPE type{TYPE::INVALID};
    std::string value;
};

enum class LEXER_STATE { DEFAULT, EAT_LINE_TO_END, VERSION_STRING };

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

using lexer_output_type = std::pair<TOKEN, LEXER_STATE>;
struct parser_output_type
{
    // the keys are the register names, and the values are (<width>, <is_classical>)
    using register_table = std::unordered_map<std::string, std::pair<size_t, bool>>;

    struct inst_type
    {
        constexpr static ssize_t ARG_IDX_ALL{-1};
        
        using fpa_type = fpa_type<512>;
        // the first element is the name of the argument, and the second is the index of the argument
        // if the operation is across an entire register, the index is ARG_IDX_ALL
        using argument_type = std::pair<std::string, ssize_t>;

        std::string name;
        std::vector<fpa_type> params;
        std::vector<argument_type> qubits;
        bool is_conditional{false};
    };

    struct gate_decl
    {
        std::string name;
        size_t num_params{0};
        size_t num_arguments{0};
        std::vector<inst_type> instructions;
    };

    std::string            oq_version;
    std::vector<inst_type> program;

    // As the user can define gates, we need to store the aliases for them.
    std::vector<gate_decl> gate_aliases;

    // maps registers to their width
    register_table register_decl;
};

// `read_next_token` invokes the lexer.
// `parse` invokes the parser, as its name suggests.

lexer_output_type  read_next_token(std::istream&, LEXER_STATE);
parser_output_type parse(std::istream&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// the behavior of the lexer is that if it is not in the default state,
// it will try to match to its current state, and if it fails, it will
// try again in the default state.

lexer_output_type lex_default_state(std::istream&, LEXER_STATE);
lexer_output_type lex_eat_line_to_end_state(std::istream&);
lexer_output_type lex_version_string_state(std::istream&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// all parser functions return the state of the lexer after consuming a sequence of tokens:

LEXER_STATE parser_handle_version(std::istream&, parser_output_type&, LEXER_STATE);
LEXER_STATE parser_handle_include(std::istream&, parser_output_type&, LEXER_STATE);
LEXER_STATE parser_handle_register(std::istream&, parser_output_type&, bool is_classical, LEXER_STATE);
LEXER_STATE parser_handle_gate_decl(std::istream&, parser_output_type&, LEXER_STATE);

// support for symbolic expressions (which is surprisingly complicated!):

namespace symbolic
{

// our strategy is to split up a symbolic expression into a sequence of addends,
// process them independently, and then sum the outcomes.
struct addend_result_type
{
    using fpa_type = parser_output_type::inst_type::fpa_type;

    fpa_type fixed_point_value{};
    double   floating_point_value{1.0};
    bool     fpa_is_valid{true};
};

// this will be created when processing exponents in an addend
struct merged_token_type
{
    struct value_type
    {
        using exponent_type = std::variant<int64_t, value_type>;

        int64_t       base;
        exponent_type exponent;
        bool          exponent_is_an_exponent{false};
    };

    std::variant<TOKEN, value_type> value;
    bool is_evaluated{false};
};

std::vector<merged_token_type> process_exponents(std::vector<TOKEN>&&);
addend_result_type             evaluate_symbolic_addend(std::vector<merged_token_type>&&);

}   // namespace symboic

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace oq2

#endif