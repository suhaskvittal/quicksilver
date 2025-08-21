/*
    author: Suhas Vittal
    date:   20 August 2025

    Main entry point for the OpenQASM 2.0 parser.
*/

#include <fstream>
#include <iostream>
#include <fstream>
#include <string>

#include "oq2/lexer_wrapper.h"
#include "parser.tab.h"

int main(int argc, char* argv[])
{
    std::ifstream file(argv[1]);
    OQ2_LEXER lexer(file);
    yy::parser parser(lexer);
    parser();
}