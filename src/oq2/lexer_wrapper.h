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

#include <fstream>
#include <iostream>
#include <string>

#include "parser.tab.h"

class OQ2_LEXER : public yyFlexLexer
{
public:
    OQ2_LEXER(std::istream& _yyin) : yyFlexLexer(_yyin, std::cout) {}

    int yylex(yy::parser::value_type* yylval);
};

#endif