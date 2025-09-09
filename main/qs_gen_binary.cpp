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

#include <zlib.h>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> void
print_stat_line(std::ostream& out, std::string_view name, T value)
{
    out << std::setw(64) << std::left << name;
    if constexpr (std::is_floating_point<T>::value)
        out << std::setw(12) << std::right << std::fixed << std::setprecision(8) << value;
    else
        out << std::setw(12) << std::right << value;
    out << "\n";
}

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

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cerr << "Usage: " << argv[0] << " <input-file> <output-file> [optional: <stats-output-file>]\n";
        return 1;
    }

    std::string input_file{argv[1]};
    std::string output_file{argv[2]};
    std::string stats_output_file{};
    ssize_t urot_precision{PROGRAM_INFO::USE_MSB_TO_DETERMINE_UROT_PRECISION};

    if (argc > 3)
        stats_output_file = std::string{argv[3]};
    if (argc > 4)
        urot_precision = std::stoi(std::string{argv[4]});

    auto stats = PROGRAM_INFO::read_from_file_and_write_to_binary(input_file, output_file, urot_precision);
    if (stats_output_file.empty())
    {
        print_stats(std::cout, stats);
    }
    else
    {
        std::ofstream stats_out(stats_output_file);
        print_stats(stats_out, stats);
    }

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////