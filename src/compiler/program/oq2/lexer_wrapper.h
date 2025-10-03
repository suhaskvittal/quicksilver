/*
    author: Suhas Vittal
    date:   20 August 2025

    This is required for interfacing Bison/Flex with C++ code.
*/

#ifndef OQ2_LEXER_H
#define OQ2_LEXER_H

#if !defined(yyFlexLexerOnce)
#include <FlexLexer.h>
#endif

#include "generic_io.h"

#include <fstream>
#include <iostream>
#include <string>

#include "parser.tab.h"

class OQ2_LEXER : public yyFlexLexer
{
private:
    generic_strm_type* real_strm_p;
public:
    // `_yyin` is unused.
    OQ2_LEXER(std::istream& _yyin, generic_strm_type* _real_strm_p) 
        :yyFlexLexer(_yyin, std::cout),
        real_strm_p(_real_strm_p)
    {}

    int yylex(yy::parser::value_type* yylval);

    // override input:
    int
    LexerInput(char* buf, int size) override
    {
        return generic_strm_read(*real_strm_p, buf, size);
    }
};

#endif