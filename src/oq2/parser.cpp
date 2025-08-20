/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#include "oq2/reader.h"

#include <iostream>

// these are gates that we use as basis gates
constexpr std::string_view RESERVED_GATES[] = 
{
    "h", "x", "y", "z", "s", "sdg", "t", "tdg", "rx", "ry", "rz", "cx", "cz", "ccx", "measure"
};

namespace oq2
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

lexer_output_type
_assert_match_token_type(std::istream& istrm, TOKEN::TYPE type, std::string_view type_name)
{
    auto out = read_next_token(istrm, LEXER_STATE::DEFAULT);
    // `out.first` is the token
    if (out.first.type != type)
        throw std::runtime_error("expected " + std::string(type_name) + " but got " + out.first.value);
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct symbolic_addend_result_type
{
    parser_output_type::inst_type::fpa_type fpa_part;
    double float_part{1.0};
    bool fpa_is_valid{true};
}

bool
_read_tokens_until_comma_or_rparen(std::istream& istrm, std::vector<TOKEN>& tokens)
{
    TOKEN tok{};
    LEXER_STATE state{LEXER_STATE::DEFAULT};
    while (tok.type != TOKEN::TYPE::RPAREN && tok.type != TOKEN::TYPE::COMMA)
    {
        std::tie(tok, state) = read_next_token(istrm, state);
        tokens.push_back(tok);
    }
    return tok.type == TOKEN::TYPE::RPAREN;
}

// the tokens provided to this function should be split off every `+` or `-`
symbolic_addend_result_type
_parser_read_symbolic_addend(std::vector<TOKEN>&& tokens)
{
    symbolic_addend_result_type out{};

    if ((tokens.size() & 1) == 0)
        throw std::runtime_error("expected an odd number of tokens for a symbolic addend");

    bool found_pi{false};
    size_t fpa_bit_idx{0};

    auto get_symbolic_value = [] (std::string_view sym) { return (sym == "pi" || sym == "PI") ? M_PI : M_E; };
    auto get_token_value = [&get_symbolic_value] (const auto& tok) 
                            {
                                return tok.type == TOKEN::TYPE::SYMBOLIC 
                                                ? get_symbolic_value(tok.value) : std::stod(tok.value);
                            };

    std::string op{"*"};  // should be `*`, `/`, `^`, or `**`

    // first merge together any exponents
    // operators should only appear on odd indices:
    std::vector<TOKEN> merged_tokens;
    merged_tokens.reserve(tokens.size());

    // start from the right most token for exponentiation:
    merged_tokens.push_back(std::move(tokens.back()));
    for (size_t i = tokens.size()-2; i >= 1; i -= 2)
    {
        if (tokens[i].type != TOKEN::TYPE::ARITHMETIC_OPERATOR)
            throw std::runtime_error("expected arithmetic operator but got " + tokens[i].value);

        TOKEN op_tok = std::move(tokens[i]);
        TOKEN next_tok = merged_tokens.back();
        TOKEN prev_tok = std::move(tokens[i-1]);
        if (op_tok.value == "**" || op_tok.value == "^")
        {
            // regardless of whether the LHS or RHS are symbolic,
            // the result will always be non-symbolic since we
            // we cannot represent such a value using a `fpa_type`
            double val_next = get_token_value(next_tok);
            double val_prev = get_token_value(prev_tok);

            // create merged token:
            double val_merged = std::pow(val_prev, val_next);
            TOKEN merged{TOKEN::TYPE::FLOAT_LITERAL, std::to_string(val_merged)};
            
            // now note that `next_tok` is currently at the end of `merged_tokens`,
            // so we need to replace it with `merged`:
            merged_tokens.back() = merged;
        }
        else
        {
            merged_tokens.push_back(op_tok);
            merged_tokens.push_back(prev_tok);
        }
    }
    // note that `merged_tokens` is in reverse order:
    std::reverse(merged_tokens.begin(), merged_tokens.end());

    // now we need to handle multiplication/division:
    // need to track a few things to determine whether the product is integral:
    // (1) has pi appeared already -- if pi appears twice, then the addend is some multiple of pi^2 (or pi^3, etc.)
    //      so we can't use `fpa_type`
    // (2) is the product an integer -- if not, then we can't use `fpa_type`
    bool product_is_integral{true};
    int64_t product_as_int{1};
    bool found_pi{false};
    for (size_t i = 1; i < merged_tokens.size() - 1; i += 2)
    {
        TOKEN op_tok = std::move(merged_tokens[i]);
        TOKEN prev_tok = std::move(merged_tokens[i-1]);
        TOKEN next_tok = std::move(merged_tokens[i+1]);
    
        if (prev_tok.type != TOKEN::TYPE::SYMBOLIC && next_tok.type != TOKEN::TYPE::SYMBOLIC)
        {
            // we can just do multiplication normally:
            if (prev_tok.type == TOKEN::TYPE::INTEGER_LITERAL && next_tok.type == TOKEN::TYPE::INTEGER_LITERAL)
                product_as_int *= std::stoll(prev_tok.value) * std::stoll(next_tok.value);
            }
            out.float_part *= get_token_value(prev_tok) * get_token_value(next_tok);
        }
    }
}

template <class PARAM_TYPE>
std::vector<PARAM_TYPE>
_parser_read_params(std::istream& istrm)
{
    std::vector<PARAM_TYPE> params;

    // there is no transition to a different lexer state from any tokens that should appear, so
    // don't worry about state preservation
    TOKEN tok{};
    LEXER_STATE state{LEXER_STATE::DEFAULT};

    std::tie(tok, state) = read_next_token(istrm, state);
    while (tok.type != TOKEN::TYPE::RPAREN)
    {
        if constexpr (std::is_same<PARAM_TYPE, std::string>::value)
        {
            // then everything should be an identifier:
            if (tok.type != TOKEN::TYPE::IDENTIFIER)
                throw std::runtime_error("expected identifier but got " + tok.value);
            params.push_back(tok.value);
        }
        else if constexpr (std::is_floating_point<PARAM_TYPE>::value)
        {
            // then everything should be a float literal:
            if (tok.type != TOKEN::TYPE::FLOAT_LITERAL)
        }
    }

    while (!istrm.eof())
    {
        std::tie(tok, state) = read_next_token(istrm, state);
        if (tok.type == TOKEN::TYPE::RPAREN)
            break;
        params.push_back(std::stol(tok.value));
    }
    return params;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

LEXER_STATE
parser_handle_version(std::istream& istrm, parser_output_type& out, LEXER_STATE state)
{
    TOKEN tok{};

    std::tie(tok, state) = _assert_match_token_type(istrm, TOKEN::TYPE::VERSION_STRING, "version string");
    out.oq_version = std::move(tok.value);

    _assert_match_token_type(istrm, TOKEN::TYPE::SEMICOLON, "';'");
    return state;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

LEXER_STATE
parser_handle_include(std::istream& istrm, parser_output_type& out, LEXER_STATE state)
{
    TOKEN tok{};

    // read the source file:
    std::tie(tok, state) = _assert_match_token_type(istrm, TOKEN::TYPE::STRING_LITERAL, "string literal");
    
    // read the source file:
    std::ifstream included_strm(tok.value);
    if (!included_strm.is_open())
        throw std::runtime_error("failed to open included file: " + tok.value);
    
    // parse the included file:
    auto inc = parse(included_strm);

    // merge the program + gate aliases from the source file.
    out.program.insert(out.program.end(), inc.program.begin(), inc.program.end());
    out.gate_aliases.insert(out.gate_aliases.end(), inc.gate_aliases.begin(), inc.gate_aliases.end());

    // finally, assert that the line ends with a semicolon    
    _assert_match_token_type(istrm, TOKEN::TYPE::SEMICOLON, "';'");
    return state;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

LEXER_STATE
parser_handle_register(std::istream& istrm, parser_output_type& out, bool is_classical, LEXER_STATE state)
{
    std::string reg_name;
    size_t reg_width{1};

    // read the register name:
    TOKEN tok{};
    std::tie(tok, state) = _assert_match_token_type(istrm, TOKEN::TYPE::IDENTIFIER, "identifier");
    reg_name = tok.value;
    
    // the register width is optional:
    std::tie(tok, state) = read_next_token(istrm, LEXER_STATE::DEFAULT);
    if (tok.type == TOKEN::TYPE::LBRACKET)
    {
        // then there is a width:
        std::tie(tok, state) = _assert_match_token_type(istrm, TOKEN::TYPE::INTEGER_LITERAL, "integer literal");
        reg_width = std::stoul(tok.value);
        _assert_match_token_type(istrm, TOKEN::TYPE::RBRACKET, "']'");
    }

    _assert_match_token_type(istrm, TOKEN::TYPE::SEMICOLON, "';'");
    return state;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

LEXER_STATE
parser_handle_gate_decl(std::istream& istrm, parser_output_type& out, LEXER_STATE state)
{
    parser_output_type::gate_decl gate_decl;

    TOKEN tok{};
    std::tie(tok, state) = _assert_match_token_type(istrm, TOKEN::TYPE::IDENTIFIER, "identifier");
    gate_decl.name = tok.value;

    // check if params are declared:
    std::tie(tok, state) = read_next_token(istrm, LEXER_STATE::DEFAULT);
    if (tok.type == TOKEN::TYPE::LPAREN)
    {
        // then, the next argument must 
    }

    _assert_match_token_type(istrm, TOKEN::TYPE::LPAREN, "(''");
    std::tie(tok, state) = read_next_token(istrm, LEXER_STATE::DEFAULT);
    if (tok.type == TOKEN::TYPE::RPAREN)
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace oq2