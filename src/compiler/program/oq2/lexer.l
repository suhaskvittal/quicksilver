/*
    author: Suhas Vittal
    date:   20 August 2025

    OpenQASM 2.0 FLEX specification
*/

%{

#include "compiler/program/oq2/lexer_wrapper.h"
#include "parser.tab.h"

#include <cmath>
#include <cstdint>
#include <string>

// redefine `YY_DECL`
#undef  YY_DECL
#define YY_DECL     int OQ2_LEXER::yylex(yy::parser::value_type* yylval)

using token = yy::parser::token;

%}

%option c++
%option yyclass="OQ2_LEXER"
%option noyywrap
%option yylineno

DECIMAL     [0-9]*"."[0-9]+

%x VERSION
%x LINE_COMMENT
%x BLOCK_COMMENT

%%

OPENQASM        { BEGIN(VERSION); return token::OPENQASM; }
include         { return token::INCLUDE; }
[qc]reg         { yylval->emplace<std::string>(yytext); return token::REGISTER; }
gate            { return token::GATE; }
opaque          { return token::OPAQUE; }
if              { return token::IF; }
measure         { return token::MEASURE; }

<VERSION>[0-9]+(\.[0-9]+)+        { 
                                yylval->emplace<std::string>(yytext); 
                                BEGIN(INITIAL); 
                                return token::VERSION_STRING;
                            }

[_a-zA-Z][_a-zA-Z0-9]*  { yylval->emplace<std::string>(yytext); return token::IDENTIFIER; }
\".+\"                  { yylval->emplace<std::string>(yytext+1, yytext+yyleng-1); return token::STRING_LITERAL; }
[0-9]+                  { yylval->emplace<int64_t>(atoll(yytext)); return token::INTEGER_LITERAL; }

{DECIMAL}                   {
                                // decimal representation of a float:
                                yylval->emplace<double>(atof(yytext));
                                return token::FLOAT_LITERAL;
                            }
{DECIMAL}[Ee][\+\-]?[0-9]+  {
                                // scientific notation of a float:
                                std::string text{yytext};
                                // find exponent start:

                                auto pos = text.find('e');
                                if (pos == std::string::npos)
                                    pos = text.find('E');

                                // extract mantissa and exponent
                                std::string mantissa_part = text.substr(0,pos);
                                std::string exponent_part = text.substr(pos+1);
                                double mantissa = std::stof(mantissa_part);
                                int exponent = std::stoi(exponent_part);

                                yylval->emplace<double>(mantissa * pow(10, exponent));
                                return token::FLOAT_LITERAL;
                            }

[\(\)\[\]\{\}\,\;]          { return yytext[0]; }


[=!]=|[><]=?                { yylval->emplace<std::string>(yytext); return token::COMPARISON_OPERATOR; }
"->"                        { return token::ARROW; }
"^"|"**"                    { return token::POWER; }
[\+\-]                      { yylval->emplace<int64_t>(yytext[0] == '-'); return token::PLUS_MINUS; }
[\*\/]                      { yylval->emplace<int64_t>(yytext[0] == '/'); return token::MULTIPLY_DIVIDE; }


[ \t\n\r]                   ;
"//"                        { BEGIN(LINE_COMMENT); }
"/*"                        { BEGIN(BLOCK_COMMENT); }

<LINE_COMMENT>[\n\r]        { BEGIN(INITIAL); }
<LINE_COMMENT>.+            ;

<BLOCK_COMMENT>"*/"         { BEGIN(INITIAL); }
<BLOCK_COMMENT>.+           ;
