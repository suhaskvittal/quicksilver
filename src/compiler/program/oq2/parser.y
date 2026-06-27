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

class OQ2Lexer;

}

%parse-param { OQ2Lexer& oq2lx }
%parse-param { ProgramInfo& prog }
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
%nterm <ProgramInfo>                               include_stmt
%nterm <compiler::prog::Register>                             register_decl

%nterm <compiler::prog::GateDefinition>                      gate_decl
%nterm <std::vector<std::string>>                   optional_gate_params
%nterm <std::vector<std::string>>                   gate_params_or_arguments
%nterm <std::vector<compiler::prog::QASMInstInfo>>          gate_decl_body

%nterm <compiler::prog::QASMInstInfo>                               conditional_instruction
%nterm <compiler::prog::QASMInstInfo>                               instruction
%nterm <std::vector<compiler::prog::Expression>>                      optional_instruction_params
%nterm <std::vector<compiler::prog::Expression>>                      instruction_params
%nterm <std::vector<compiler::prog::QASMOperand>>    instruction_arguments
%nterm <compiler::prog::QASMInstInfo>                               measurement

%nterm <compiler::prog::Expression>                   expression
%nterm <compiler::prog::Term>                         term
%nterm <compiler::prog::ExponentialValue>            signed_expval;
%nterm <compiler::prog::ExponentialValue>            expval;
%nterm <compiler::prog::generic_value_type>           generic_value
%nterm <compiler::prog::QASMOperand> argument;

%%

program: OPENQASM VERSION_STRING ';' line   { prog.version = $2; }
        | line                               { prog.version = "2.0"; }    
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
                                                $$ = ProgramInfo::from_file(file_to_read);
                                            }
            ; 

register_decl: REGISTER argument ';'        {
                                                $$.type = ($1 == "creg") ? compiler::prog::Register::Type::Bit
                                                                         : compiler::prog::Register::Type::Qubit;
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
                            $$ = compiler::prog::QASMInstInfo{$1, $2, $3};
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

expression: term                                {
                                                    compiler::prog::TermEntry entry;
                                                    entry.term = $1;
                                                    entry.operator_with_previous = compiler::prog::Operator::ADD;
                                                    $$.terms.push_back(entry);
                                                }
            | expression PLUS_MINUS term        {
                                                    auto op = static_cast<compiler::prog::Operator>($2);
                                                    $$ = std::move($1);
                                                    compiler::prog::TermEntry entry;
                                                    entry.term = $3;
                                                    entry.operator_with_previous = op;
                                                    $$.terms.push_back(entry);
                                                }
            ;
term: term MULTIPLY_DIVIDE signed_expval        {
                                                    auto op = static_cast<compiler::prog::Operator>($2 + 2);
                                                    $$ = std::move($1);
                                                    compiler::prog::Factor factor;
                                                    factor.exponential_value = $3;
                                                    factor.operator_with_previous = op;
                                                    $$.factors.push_back(factor);
                                                }
    | signed_expval                             {
                                                    compiler::prog::Factor factor;
                                                    factor.exponential_value = $1;
                                                    factor.operator_with_previous = compiler::prog::Operator::MULTIPLY;
                                                    $$.factors.push_back(factor);
                                                }
    ;
signed_expval: expval                           { $$ = $1; }
             | PLUS_MINUS expval                {
                                                    $2.is_negated = ($1 > 0);
                                                    $$ = std::move($2);
                                                }
             ;
expval: generic_value                           { $$.power_sequence.push_back($1); }
        | expval POWER generic_value            { $$ = std::move($1); $$.power_sequence.push_back($3); }
        ;
generic_value: INTEGER_LITERAL                  { $$ = $1; }
            | FLOAT_LITERAL                     { $$ = $1; }
            | IDENTIFIER                        { $$ = $1; }
            | '(' expression ')'                {
                                                    compiler::prog::expr_ptr e_p{new compiler::prog::Expression};
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
