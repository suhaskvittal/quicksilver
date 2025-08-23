/*
    author: Suhas Vittal
    date:   20 August 2025

    OpenQASM 2.0 BISON specification
*/

%require "3.2"
%language "c++"

%code requires
{

#include "program.h"

#include <cstdint>
#include <string>

class OQ2_LEXER;

}

%parse-param { OQ2_LEXER& oq2lx }
%define api.value.type variant
%define parse.error verbose

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

// nonterminals (see `oq2/types.h`)
%nterm <PROGRAM_INFO>                               program
%nterm <PROGRAM_INFO>                               line
%nterm <PROGRAM_INFO>                               include_stmt
%nterm <oq2::REGISTER>                              register_decl

%nterm <prog::GATE_DEFINITION>                      gate_decl
%nterm <std::vector<std::string>>                   optional_gate_params
%nterm <std::vector<std::string>>                   gate_params_or_arguments
%nterm <std::vector<prog::QASM_INST_INFO>>          gate_decl_body

%nterm <prog::QASM_INST_INFO>                               conditional_instruction
%nterm <prog::QASM_INST_INFO>                               instruction
%nterm <std::vector<prog::EXPRESSION>>                      optional_instruction_params
%nterm <std::vector<prog::EXPRESSION>>                      instruction_params
%nterm <std::vector<prog::QASM_INST_INFO::operand_type>>    instruction_arguments
%nterm <prog::QASM_INST_INFO>                               measurement

%nterm <prog::EXPRESSION>                           expression
%nterm <prog::EXPRESSION::term_type>                term
%nterm <prog::EXPRESSION::exponential_value_type>   signed_expval;
%nterm <prog::EXPRESSION::exponential_value_type>   expval;
%nterm <prog::EXPRESSION::generic_value_type>       generic_value
%nterm <prog::QASM_INST_INFO::operand_type>         argument;

%%

program: OPENQASM VERSION_STRING ';' line   { $$ = std::move($4); $$.version_ = $2; }
        ;

line: line include_stmt                     { $$ = std::move($1); $$.merge(std::move($2)); }
    | line register_decl                    { $$ = std::move($1); $$.declare_register(std::move($2)); }
    | line gate_decl                        { $$ = std::move($1); $$.declare_gate(std::move($2)); }
    | line conditional_instruction          { $$ = std::move($1); $$.add_instruction(std::move($2)); }
    | line instruction                      { $$ = std::move($1); $$.add_instruction(std::move($2)); }
    | line measurement                      { $$ = std::move($1); $$.add_instruction(std::move($2)); }
    ;

include_stmt: INCLUDE STRING_LITERAL ';'    {
                                                std::string file_to_read = $2;
                                                /* parse file here... */
                                            }
            ; 

register_decl: REGISTER argument ';'        {
                                                $$.type = ($1 == "creg") ? REGISTER::TYPE::BIT
                                                                         : REGISTER::TYPE::QUBIT;
                                                $$.name = $2.name;
                                                if ($2.index != NO_INDEX)
                                                    $$.width = $2.index;
                                            }
            ;

gate_decl: GATE IDENTIFIER optional_gate_params gate_params_or_arguments '{' gate_decl_body '}'
                    {
                        $$.name = $2;
                        $$.params = std::move($3);
                        $$.args = std::move($4);
                        $$.body = std::move($6);
                    }
            ;
optional_gate_params: '(' gate_params_or_arguments ')'          { $$ = $2; }
                    | 
                    ;
gate_params_or_arguments: IDENTIFIER                            { $$.push_back($1); }
                    | gate_params_or_arguments ',' IDENTIFIER   { $$ = std::move($1); $$.push_back($3); }
                    ;
gate_decl_body: gate_decl_body instruction                      { $$ = std::move($1); $$.push_back($2); }
                |
                ;

conditional_instruction: IF '(' IDENTIFIER COMPARISON_OPERATOR expression ')' instruction
                        {
                            $$ = std::move($1);
                            $$.conditional = true;
                        }
            ;
instruction: IDENTIFIER optional_instruction_params instruction_arguments ';'  
                        {
                            $$ = QASM_INST_INFO{$1, $2, $3};
                        }
            ;
optional_instruction_params: '(' instruction_params ')'     { $$ = $2; }
                            |
                            ;
instruction_params: instruction_params ',' expression       { $$ = std::move($1); $$.push_back($3); }
                    | expression                            { $$.push_back($1); }
                    ;
instruction_arguments: instruction_arguments ',' argument   { $$ = std::move($1); $$.push_back($3); }
                    | argument                              { $$.push_back($1); }
                    ;

measurement: MEASURE argument ARROW argument ';'    { 
                                                        $$.gate_name = "measure";
                                                        $$.args = {$2, $4};
                                                    }
            ;

expression: term                                { $$.emplace_back($1, OPERATOR::ADD); }
            | expression PLUS_MINUS term        { 
                                                    auto op = ($2 == "-") ? EXPRESSION::OPERATOR::ADD
                                                                          : EXPRESSION::OPERATOR::SUBTRACT;
                                                    $$ = std::move($1);
                                                    $$.emplace_back($3, op);
                                                }
            ;
term: term MULTIPLY_DIVIDE signed_expval        {
                                                    auto op = ($2 == "*") ? EXPRESSION::OPERATOR::MULTPLY
                                                                          : EXPRESSION::OPERATOR::DIVIDE;
                                                    $$ = std::move($1); 
                                                    $$.emplace_back($2, op);
                                                }
    | signed_expval                             {  $$.emplace_back($1, EXPRESSION::OPERATOR::MULTIPLY); }
    ;
signed_expval: expval
             | PLUS_MINUS expval                {
                                                    if ($1 == "-")
                                                        $2.second = true;
                                                    $$ = std::move($2);
                                                }
             ;
expval: generic_value                           { $$.first.push_back($1); }
        | expval POWER generic_value            { $$ = std::move($1); $$.first.push_back($3); }
        ;
generic_value: INTEGER_LITERAL                  { $$.emplace<int64_t>($1); }
            | FLOAT_LITERAL                     { $$.emplace<double>($1); }
            | IDENTIFIER                        { $$.emplace<std::string>($1); }
            | '(' expression ')'                {  
                                                    auto expr_p = std::make_unique<EXPRESSION>();
                                                    *expr_p = $1;
                                                    $$.emplace<std::unique_ptr<EXPRESSION>>(std::move(expr_p));
                                                }
            ;

argument: IDENTIFIER                            { $$.name = $1; }
        | IDENTIFIER '[' INTEGER_LITERAL ']'    { $$ = prog::QASM_INST_INFO::operand_type{$1,$3}; }
        ;

%%

void
yy::parser::error(const std::string& msg)
{
    std::cerr << "Error: " << msg << "\n";
}