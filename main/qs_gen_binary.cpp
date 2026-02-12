/*
    author: Suhas Vittal
    date:   20 August 2025

    Main entry point for the OpenQASM 2.0 parser.
*/

#include <fstream>
#include <iostream>
#include <fstream>
#include <string>

#include "argparse.h"
#include "compiler/program/oq2/lexer_wrapper.h"
#include "compiler/program/rotation_manager.h"
#include "parser.tab.h"
#include "compiler/program.h"

#include <zlib.h>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

void
print_stats(std::ostream& out, const PROGRAM_INFO::stats_type& stats)
{
    // print statistics:
    print_stat_line(out, "TOTAL_GATE_COUNT", stats.total_gate_count);
    print_stat_line(out, "SOFTWARE_GATE_COUNT", stats.software_gate_count);
    print_stat_line(out, "T_GATE_COUNT", stats.t_gate_count);
    print_stat_line(out, "CXZ_GATE_COUNT", stats.cxz_gate_count);
    print_stat_line(out, "ROTATION_COUNT", stats.rotation_count);
    print_stat_line(out, "CCXZ_COUNT", stats.ccxz_count);
    print_stat_line(out, "VIRTUAL_INSTRUCTION_COUNT", stats.virtual_inst_count);
    print_stat_line(out, "UNROLLED_INSTRUCTION_COUNT", stats.unrolled_inst_count);
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int main(int argc, char* argv[])
{
    std::string input_file;
    std::string output_file;
    std::string stats_output_file;

    ARGPARSE()
        .required("input-file", "input file qasm file (can be compressed)", input_file)
        .required("output-file", "output file binary (.bin or .gz only)", output_file)
        .optional("-s", "--stats-output-file", "output file for statistics (.txt -- default is no stats)", stats_output_file, "")
        .optional("-p", "--print-progress", "the number of instructions to print progress", prog::GL_PRINT_PROGRESS, 1'000'000)
        .optional("-rpc", "--rotation-recomputation-isa", "use RPC isa (argument of this option is the level)", GL_USE_RPC_ISA, 0)
        .parse(argc, argv);

    prog::rotation_manager_init();
    auto stats = PROGRAM_INFO::read_from_file_and_write_to_binary(input_file, output_file);
    std::cout << "DONE\n";

    if (stats_output_file.empty())
    {
        print_stats(std::cout, stats);
    }
    else
    {
        std::ofstream stats_out(stats_output_file);
        print_stats(stats_out, stats);
    }

    prog::rotation_manager_end();
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
