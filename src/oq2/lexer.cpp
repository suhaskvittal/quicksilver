/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#include "oq2/reader.h"

#include <iostream>

namespace oq2
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class PRED_TYPE> char
_istream_get_char_and_throw_on_fail(std::istream& istrm, const PRED_TYPE& pred, std::string_view current_token_buf)
{
    if (istrm.eof())
        throw std::runtime_error("unexpected end of file while parsing token: " + std::string(current_token_buf));
    char c = istrm.get();
    if (!pred(c))
        throw std::runtime_error("unexpected character while parsing token: " + std::string(current_token_buf));
    return c;
}

char
_istream_get_char_and_throw_on_fail(std::istream& istrm, char match, std::string_view current_token_buf)
{
    return _istream_get_char_and_throw_on_fail(istrm, [match](char c) { return c == match; }, current_token_buf);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TOKEN::TYPE
_lex_integer_or_float_literal(std::istream& istrm, std::string& buf)
{
    // can be a decimal literal or float literal.
    // note that a float literal can be in scientific notation (i.e., 1.0e-10)
    bool hit_decimal{false};
    bool hit_exponent{false};
    bool hit_exp_sign{false};

    while (isdigit(c) 
            || (!hit_decimal && c == '.') 
            || (!hit_exponent && (c == 'e' || c == 'E')) 
            || (!hit_exp_sign && (c == '-' || c == '+')))
    {
        hit_decimal |= c == '.';
        hit_exponent |= c == 'e' || c == 'E';
        hit_exp_sign |= c == '-' || c == '+';

        buf.push_back(c);
        c = istrm.get();
    }
    istrm.putback(c);
    return hit_decimal ? TOKEN::TYPE::FLOAT_LITERAL : TOKEN::TYPE::INTEGER_LITERAL;
}

lexer_output_type
lex_default_state(std::istream& istrm, LEXER_STATE current_state)
{
    if (istrm.eof())
        return {TOKEN{TOKEN::TYPE::INVALID}, LEXER_STATE::DEFAULT};

    std::string buf{};
    TOKEN::TYPE tok_type{TOKEN::TYPE::INVALID};
    LEXER_STATE next_state{current_state};

    char c = istrm.get();
    
    // keywords, identifiers, and literals:
    if (c == '_')
    {
        // this can only be a identifier:
        while (isalpha(c) || isdigit(c) || c == '_')
        {
            buf.push_back(c);
            c = istrm.get();
        }
        istrm.putback(c);
        tok_type = TOKEN::TYPE::IDENTIFIER;
    }
    else if (isalpha(c))
    { 
        // can be an identifier or a reserved keyword (i.e., OPENQASM)
        while (isalpha(c) || isdigit(c) || c == '_')
        {
            buf.push_back(c);
            c = istrm.get();
        }
        istrm.putback(c);

        // now check if it's a reserved keyword and default to `IDENTIFIER` otherwise:
        if (buf == "OPENQASM" || buf == "openqasm")
            tok_type = TOKEN::TYPE::OPENQASM;
        else if (buf == "include")
            tok_type = TOKEN::TYPE::INCLUDE;
        else if (buf == "qreg" || buf == "creg")
            tok_type = TOKEN::TYPE::REGISTER;
        else if (buf == "gate")
            tok_type = TOKEN::TYPE::GATE;
        else if (buf == "opaque")
            tok_type = TOKEN::TYPE::OPAQUE;
        else if (buf == "if")
            tok_type = TOKEN::TYPE::IF;
        else if (buf == "pi" || buf == "e" || buf == "PI" || buf == "E")
            tok_type = TOKEN::TYPE::SYMBOLIC;
        else
            tok_type = TOKEN::TYPE::IDENTIFIER;
    }
    else if (isdigit(c))
    {
        tok_type = _lex_integer_or_float_literal(istrm, buf);
    }
    else if (c == '"')
    {
        // always a string literal:
        while ((c=istrm.get()) != '"')
        {
            buf.push_back(c);
            c = istrm.get();
        }
        istrm.putback(c);
        tok_type = TOKEN::TYPE::STRING_LITERAL;
    }

    // delimiters:
    else if (c == '(') return {TOKEN{TOKEN::TYPE::LPAREN}, LEXER_STATE::DEFAULT};
    else if (c == ')') return {TOKEN{TOKEN::TYPE::RPAREN}, LEXER_STATE::DEFAULT};
    else if (c == '[') return {TOKEN{TOKEN::TYPE::LBRACKET}, LEXER_STATE::DEFAULT};
    else if (c == ']') return {TOKEN{TOKEN::TYPE::RBRACKET}, LEXER_STATE::DEFAULT};
    else if (c == '{') return {TOKEN{TOKEN::TYPE::LBRACE}, LEXER_STATE::DEFAULT};
    else if (c == '}') return {TOKEN{TOKEN::TYPE::RBRACE}, LEXER_STATE::DEFAULT};
    else if (c == ',') return {TOKEN{TOKEN::TYPE::COMMA}, LEXER_STATE::DEFAULT};
    else if (c == ';') return {TOKEN{TOKEN::TYPE::SEMICOLON}, LEXER_STATE::DEFAULT};
    
    // operators:
    else if (c == '=' || c == '!')
    {
        buf.push_back(c);
        // the next character must be an equals sign
        c = _istream_get_char_and_throw_on_fail(istrm, '=', buf);
        buf.push_back(c);
        tok_type = TOKEN::TYPE::COMPARISON_OPERATOR;
    }
    else if (c == '>' || c == '<')
    {
        // the next character `might` be an equals sign
        buf.push_back(c);
        c = istrm.get();
        if (c == '=')
            buf.push_back(c);
        else
            istrm.putback(c);
        tok_type = TOKEN::TYPE::COMPARISON_OPERATOR;
    }
    else if (c == '+')
    {
        buf.push_back(c);
        tok_type = TOKEN::TYPE::ARITHMETIC_OPERATOR;
    }
    else if (c == '-')
    {
        buf.push_back(c);
        // might be an arrow operator or integer/float literal:
        c = istrm.get();
        if (c == '>')
        {
            buf.push_back(c);
            tok_type = TOKEN::TYPE::ARROW;
        }
        else if (isdigit(c))
        {
            buf.push_back(c);
            tok_type = _lex_integer_or_float_literal(istrm, buf);
        }
        else
        {
            istrm.putback(c);
            tok_type = TOKEN::TYPE::ARITHMETIC_OPERATOR;
        }
    }
    else if (c == '*')
    {
        buf.push_back(c);
        // could be a power operator:
        c = istrm.get();
        if (c == '*')
            buf.push_back(c);
        else
            istrm.putback(c);
        tok_type = TOKEN::TYPE::ARITHMETIC_OPERATOR;
    }
    else if (c == '/')
    {
        buf.push_back(c);
        // could be a comment:
        c = istrm.get();
        if (c == '/')
        {
            tok_type = TOKEN::TYPE::COMMENT;
            next_state = LEXER_STATE::EAT_LINE_TO_END;
        }
        else
        {
            istrm.putback(c);
            tok_type = TOKEN::TYPE::ARITHMETIC_OPERATOR;
        }
    }

    // whitespace: (note that comment is covered when `c == '/'`)
    else if (std::isspace(c))
    {
        // consume all whitespace until we get a non-whitespace character
        while (std::isspace(c))
            c = istrm.get();
        istrm.putback(c);
        tok_type = TOKEN::TYPE::WHITESPACE;
    }

    return lexer_output_type{TOKEN{tok_type, buf}, next_state};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

lexer_output_type
lex_eat_line_to_end_state(std::istream& istrm)
{
    char c = istrm.get();
    LEXER_STATE next_state{LEXER_STATE::EAT_LINE_TO_END};
    TOKEN::TYPE tok_type{TOKEN::TYPE::INVALID};

    if (c == '\n' || c == '\r')
    {
        tok_type = TOKEN::TYPE::EOL;
        next_state = LEXER_STATE::DEFAULT;
    }
    else
    {
        // keep reading until we get a newline or EOF
        while (!istrm.eof() && c != '\n' && c != '\r')
            c = istrm.get();
        istrm.putback(c);
        tok_type = TOKEN::TYPE::LINE_CONTENT;
    }

    return lexer_output_type{TOKEN{tok_type}, next_state};
}

lexer_output_type
lex_version_string_state(std::istream& istrm)
{
    char c = istrm.get();
    LEXER_STATE next_state{LEXER_STATE::VERSION_STRING};
    TOKEN::TYPE tok_type{TOKEN::TYPE::INVALID};
    std::string buf{};
    buf.reserve(8);

    
    // try and match the version string
    bool last_char_was_dot{false};
    if (isdigit(c))
    {
        while (isdigit(c) || (!last_char_was_dot && c == '.'))
        {
            last_char_was_dot = c == '.';
            buf.push_back(c);
            c = istrm.get();
        }
        istrm.putback(c);
        tok_type = TOKEN::TYPE::VERSION_STRING;
        next_state = LEXER_STATE::DEFAULT;
    }
    else
    {
        istrm.putback(c);
    }

    return lexer_output_type{TOKEN{tok_type, buf}, next_state};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace oq2