/*
    author: Suhas Vittal
    date:   20 August 2025

    OpenQASM 2.0 BISON specification
*/

%require "3.2"
%language "c++"

%code requires
{

#include "compiler/program.h"

#include <cstdint>
#include <string>

class OQ2_LEXER;

}

%parse-param { OQ2_LEXER& oq2lx }
%parse-param { PROGRAM_INFO& prog }
%parse-param { std::string curr_relative_path }

%define api.value.type variant
%define parse.error verbose
%define parse.trace

%code
{

#include "compiler/program/oq2/lexer_wrapper.h"
#define yylex oq2lx.yylex

}

// Keyword tokens:
%token                  OPENQASM
%token                  INCLUDE
%token <std::string>    REGISTER
%token                  GATE
%token                  OPAQUE
%token                  IF
%token                  MEASURE

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
%nterm                                              program
%nterm                                              line
%nterm <PROGRAM_INFO>                               include_stmt
%nterm <prog::REGISTER>                             register_decl

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

program: OPENQASM VERSION_STRING ';' line   { prog.version_ = $2; }
        | line                               { prog.version_ = "2.0"; }    
        ;

line: line include_stmt                     { prog.merge(std::move($2)); }
    | line register_decl                    { prog.declare_register(std::move($2)); }
    | line gate_decl                        { prog.declare_gate(std::move($2)); }
    | line conditional_instruction          { prog.add_instruction(std::move($2)); }
    | line instruction                      { prog.add_instruction(std::move($2)); }
    | line measurement                      { prog.add_instruction(std::move($2)); }
    | /* empty */                           
    ;

include_stmt: INCLUDE STRING_LITERAL ';'    {
                                                std::string file_to_read = $2;
                                                if (file_to_read == "qelib1.inc")
                                                    file_to_read = QELIB1_INC_PATH;
                                                else
                                                    file_to_read = curr_relative_path + "/" + file_to_read;
                                                $$ = PROGRAM_INFO::from_file(file_to_read, prog.urot_precision_);
                                            }
            ; 

register_decl: REGISTER argument ';'        {
                                                $$.type = ($1 == "creg") ? prog::REGISTER::TYPE::BIT
                                                                         : prog::REGISTER::TYPE::QUBIT;
                                                $$.name = $2.name;
                                                if ($2.index >= 0)
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
                    | /* empty */                               { $$ = {}; }
                    ;
gate_params_or_arguments: IDENTIFIER                            { $$.push_back($1); }
                    | gate_params_or_arguments ',' IDENTIFIER   { $$ = std::move($1); $$.push_back($3); }
                    ;
gate_decl_body: gate_decl_body instruction                      { $$ = std::move($1); $$.push_back($2); }
                | /* empty */                                   { $$ = {}; }
                ;

conditional_instruction: IF '(' IDENTIFIER COMPARISON_OPERATOR expression ')' instruction
                        {
                            $$ = std::move($7);
                            $$.is_conditional = true;
                        }
            ;
instruction: IDENTIFIER optional_instruction_params instruction_arguments ';'  
                        {
                            $$ = prog::QASM_INST_INFO{$1, $2, $3};
                        }
            ;
optional_instruction_params: '(' instruction_params ')'     { $$ = $2; }
                            | /* empty */                   { $$ = {}; }
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

expression: term                                { $$.termseq.emplace_back($1, prog::EXPRESSION::OPERATOR::ADD); }
            | expression PLUS_MINUS term        { 
                                                    auto op = static_cast<prog::EXPRESSION::OPERATOR>($2);
                                                    $$ = std::move($1);
                                                    $$.termseq.emplace_back($3, op);
                                                }
            ;
term: term MULTIPLY_DIVIDE signed_expval        {
                                                    auto op = static_cast<prog::EXPRESSION::OPERATOR>($2 + 2);
                                                    $$ = std::move($1); 
                                                    $$.emplace_back($3, op);
                                                }
    | signed_expval                             {  $$.emplace_back($1, prog::EXPRESSION::OPERATOR::MULTIPLY); }
    ;
signed_expval: expval                           { $$ = $1; }
             | PLUS_MINUS expval                {
                                                    $2.second = ($1 > 0);
                                                    $$ = std::move($2);
                                                }
             ;
expval: generic_value                           { $$.first.push_back($1); }
        | expval POWER generic_value            { $$ = std::move($1); $$.first.push_back($3); }
        ;
generic_value: INTEGER_LITERAL                  { $$ = $1; }
            | FLOAT_LITERAL                     { $$ = $1; }
            | IDENTIFIER                        { $$ = $1; }
            | '(' expression ')'                { 
                                                    prog::EXPRESSION::expr_ptr e_p{new prog::EXPRESSION}; 
                                                    *e_p = $2;
                                                    $$ = std::move(e_p); 
                                                }
            ;

argument: IDENTIFIER                            { $$.name = $1; }
        | IDENTIFIER '[' INTEGER_LITERAL ']'    { $$.name = $1; $$.index = $3; }
        ;

%%

void
yy::parser::error(const std::string& msg)
{
    std::cerr << "Error: " << msg << "\n";
    exit(1);
}