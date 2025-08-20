/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#include "oq2/reader.h"

namespace oq2
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

lexer_output_type
read_next_token(std::istream& istrm, LEXER_STATE state)
{
    TOKEN tok{TOKEN::TYPE::INVALID};
    do
    {
        switch (state)
        {
            case LEXER_STATE::EAT_LINE_TO_END:
                std::tie(tok, state) = lex_eat_line_to_end_state(istrm);
            case LEXER_STATE::VERSION_STRING:
                std::tie(tok, state) = lex_version_string_state(istrm);
        }

        // either we haven't done anything, or we failed to match whilst in an alternative state
        if (tok.type == TOKEN::TYPE::INVALID)
            std::tie(tok, state) = lex_default_state(istrm, state);
    }
    while (tok.type == TOKEN::TYPE::WHITESPACE 
            || tok.type == TOKEN::TYPE::COMMENT 
            || tok.type == TOKEN::TYPE::LINE_CONTENT
            || tok.type == TOKEN::TYPE::EOL);
    return {tok, state};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

parser_output_type
parse(std::istream& istrm)
{
    parser_output_type out{};

    TOKEN tok{};
    LEXER_STATE state{LEXER_STATE::DEFAULT};
    while (!istrm.eof())
    {
        std::tie(tok, state) = read_next_token(istrm, state);

        // first, check if this is a version:
        if (tok.type == TOKEN::TYPE::OPENQASM)
            state = parser_handle_version(istrm, out, state);
        else if (tok.type == TOKEN::TYPE::INCLUDE)
            state = parser_handle_include(istrm, out, state);
        else if (tok.type == TOKEN::TYPE::REGISTER)
            state = parser_handle_register(istrm, out, tok.value == "creg", state);
        else if (tok.type == TOKEN::TYPE::GATE)
            state = parser_handle_gate_decl(istrm, out, state);
        else if (tok.type == TOKEN::TYPE::OPAQUE)
            throw std::runtime_error("OPAQUE keyword is unsupported yet (don't know what it means)");
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace oq2