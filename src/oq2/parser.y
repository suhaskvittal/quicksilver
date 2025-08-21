/*
    author: Suhas Vittal
    date:   20 August 2025

    OpenQASM 2.0 BISON specification
*/

%require "3.2"
%language "c++"

%code requires
{

#include <cstdint>
#include <string>

class OQ2_LEXER;

}

%parse-param { OQ2_LEXER& oq2lx }
%define api.value.type variant

%code
{

#include "oq2/lexer_wrapper.h"
#define yylex oq2lx.yylex


}

// Keyword tokens:
%token OPENQASM
%token INCLUDE
%token REGISTER
%token GATE
%token OPAQUE
%token IF
%token MEASURE

// Identifier and literal tokens:
%token <int64_t>        INTEGER_LITERAL;
%token <double>         FLOAT_LITERAL;
%token <std::string>    IDENTIFIER;
%token <std::string>    STRING_LITERAL;

// Operators:
%token <std::string>    COMPARISON_OPERATOR;
%token                  ARROW;
%left  <int64_t>        PLUS_MINUS;
%left  <int64_t>        MULTIPLY_DIVIDE;
%right                  POWER;

// Other:
%token <std::string>    VERSION_STRING;

%%

program: OPENQASM VERSION_STRING ';' line
        ;

line:
    | line line_content
    ;

line_content: include_stmt
            | register_decl
            | gate_decl
            | conditional_instruction
            | instruction
            | measurement
            ;

include_stmt: INCLUDE STRING_LITERAL ';'
            ; 

register_decl: REGISTER argument ';'
            ;

gate_decl: GATE IDENTIFIER optional_gate_params gate_params_or_arguments '{' gate_decl_body '}'
            ;
optional_gate_params: '(' gate_params_or_arguments ')'
                    | 
                    ;
gate_params_or_arguments: IDENTIFIER
                    | IDENTIFIER ',' gate_params_or_arguments
                    ;
gate_decl_body: gate_decl_body instruction ';'
                |
                ;

conditional_instruction: IF '(' IDENTIFIER COMPARISON_OPERATOR expression ')' instruction
            ;
instruction: IDENTIFIER optional_instruction_params instruction_arguments ';'
            ;
optional_instruction_params: '(' instruction_params ')'
                            |
                            ;
instruction_params: instruction_params ',' expression
                    | expression
                    ;
instruction_arguments: instruction_arguments ',' argument
                    | argument
                    ;

measurement: MEASURE argument ARROW argument ';'
            ;

expression: term
            | expression PLUS_MINUS term
            ;
term: term MULTIPLY_DIVIDE signed_expval
    | signed_expval
    ;
signed_expval: expval
             | PLUS_MINUS expval
             ;
expval: expval POWER generic_value
        | generic_value
        ;
generic_value: INTEGER_LITERAL
            | FLOAT_LITERAL
            | IDENTIFIER
            | '(' expression ')'
            ;

argument: IDENTIFIER
        | IDENTIFIER '[' INTEGER_LITERAL ']'
        ;

%%

void
yy::parser::error(const std::string& msg)
{
    std::cerr << "Error: " << msg << "\n";
}