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
print_stats(std::ostream& out, uint64_t num_gates_post_opt, const PROGRAM_INFO::stats_type& stats)
{
    // print statistics:
    print_stat_line(out, "TOTAL_GATE_COUNT", num_gates_post_opt);
    print_stat_line(out, "SOFTWARE_GATE_COUNT", stats.software_gate_count);
    print_stat_line(out, "T_GATE_COUNT", stats.t_gate_count);
    print_stat_line(out, "CXZ_GATE_COUNT", stats.cxz_gate_count);
    print_stat_line(out, "ROTATION_COUNT", stats.rotation_count);
    print_stat_line(out, "CCXZ_COUNT", stats.ccxz_count);
    print_stat_line(out, "VIRTUAL_INSTRUCTION_COUNT", stats.virtual_inst_count);
    print_stat_line(out, "UNROLLED_INSTRUCTION_COUNT", stats.unrolled_inst_count);
    print_stat_line(out, "MEAN_INSTRUCTION_LEVEL_PARALLELISM", stats.mean_instruction_level_parallelism);
    print_stat_line(out, "MEAN_CONCURRENT_ROTATION_COUNT", stats.mean_concurrent_rotation_count);
    print_stat_line(out, "MEAN_CONCURRENT_CXZ_COUNT", stats.mean_concurrent_cxz_count);
    print_stat_line(out, "MEAN_ROTATION_UNROLLED_COUNT", stats.mean_rotation_unrolled_count);
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

    if (argc > 3)
        stats_output_file = std::string{argv[3]};

    PROGRAM_INFO prog = PROGRAM_INFO::from_file(input_file);

    prog.dead_gate_elimination();
    size_t num_gates_post_opt = prog.get_instructions().size();

    if (stats_output_file.empty())
    {
        print_stats(std::cout, num_gates_post_opt, prog.analyze_program());
    }
    else
    {
        std::ofstream stats_out(stats_output_file);
        print_stats(stats_out, num_gates_post_opt, prog.analyze_program());
    }
    const uint32_t num_qubits = static_cast<uint32_t>(prog.get_num_qubits());

    // write binary file:
    bool is_gz_file = output_file.find(".gz") != std::string::npos;

    if (is_gz_file)
    {
        gzFile gzstrm = gzopen(output_file.c_str(), "wb");
        
        gzwrite(gzstrm, &num_qubits, 4);

        auto write_func = [&gzstrm] (const void* data, size_t size) { gzwrite(gzstrm, data, size); };
        for (const auto& inst : prog.get_instructions())
        {
            auto enc = inst.serialize();
            enc.read_write(write_func);
        }
        gzclose(gzstrm);
    }
    else
    {
        std::ofstream strm(output_file, std::ios::binary);

        strm.write(reinterpret_cast<const char*>(&num_qubits), 4);
         
        auto write_func = [&strm] (const void* data, size_t size) { strm.write(static_cast<const char*>(data), size); };
        for (const auto& inst : prog.get_instructions())
        {
            auto enc = inst.serialize();
            enc.read_write(write_func);
        }
    }

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////