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
#include "program.h"

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        std::cerr << "Usage: " << argv[0] << " <qasm_file>\n";
        return 1;
    }

    PROGRAM_INFO prog = PROGRAM_INFO::from_file(argv[1]);

    size_t num_gates_pre_opt = prog.get_instructions().size();
    size_t num_gates_removed = prog.dead_gate_elimination();
    size_t num_gates_post_opt = prog.get_instructions().size();

    std::cout << "number of gates before optimization: " << num_gates_pre_opt << "\n";
    std::cout << "number of gates removed after dead gate elimination: " << num_gates_removed << "\n";
    std::cout << "number of gates after optimization: " << num_gates_post_opt << "\n";
    
    if (num_gates_post_opt < 10000)
    {
        std::cout << "\n\n";
        for (const auto& inst : prog.get_instructions())
            std::cout << inst << "\n";
    }
}