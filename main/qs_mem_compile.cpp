/*
    author: Suhas Vittal
    date:   23 September 2025
*/

#include "argparse.h"
#include "generic_io.h"
#include "instruction.h"
#include "compiler/memopt.h"

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> void
print_stat_line(std::ostream& out, std::string name, T value)
{
    out << std::setw(64) << std::left << name;
    if constexpr (std::is_floating_point<T>::value)
        out << std::setw(12) << std::right << std::fixed << std::setprecision(8) << value;
    else
        out << std::setw(12) << std::right << value;
    out << "\n";
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr int64_t EMIT_IMPL_ID_VISZLAI = static_cast<int64_t>(MEMOPT::EMIT_IMPL_ID::VISZLAI);

int
main(int argc, char** argv)
{
    std::string input_trace_file;
    std::string output_trace_file;
    int64_t inst_limit;
    int64_t cmp_count;
    int64_t print_progress_freq;
    int64_t emit_impl_id;

    ARGPARSE()
        .required("input-file", "the trace file (with no memory instructions) to compile", input_trace_file)
        .required("output-file", "the output trace file path", output_trace_file)
        .optional("-i", "--inst-limit", "number of instructions to compile", inst_limit, std::numeric_limits<uint64_t>::max())
        .optional("-c", "--cmp-count", "number of surface codes within compute to assume", cmp_count, 4)
        .optional("-pp", "--print-progress", "print progress frequency", print_progress_freq, 100000)
        .optional("-e", "--emit-impl", "emit implementation", emit_impl_id, EMIT_IMPL_ID_VISZLAI)
        .parse(argc, argv);

    MEMOPT::EMIT_IMPL_ID emit_impl = static_cast<MEMOPT::EMIT_IMPL_ID>(emit_impl_id);

    // read the file:
    generic_strm_type istrm, ostrm;

    generic_strm_open(istrm, input_trace_file, "rb");
    generic_strm_open(ostrm, output_trace_file, "wb");

    MEMOPT compiler(cmp_count, emit_impl, print_progress_freq);
    compiler.run(istrm, ostrm, inst_limit);

    generic_strm_close(istrm);
    generic_strm_close(ostrm);

    print_stat_line(std::cout, "INST_DONE", compiler.s_inst_done);
    print_stat_line(std::cout, "UNROLLED_INST_DONE", compiler.s_unrolled_inst_done);
    print_stat_line(std::cout, "MEMORY_INSTRUCTIONS", compiler.s_memory_instructions_added);
    print_stat_line(std::cout, "MEMORY_PREFETCHES", compiler.s_memory_prefetches_added);
    print_stat_line(std::cout, "EMISSION_CALLS", compiler.s_emission_calls);
    print_stat_line(std::cout, "TOTAL_TIMESTEPS", compiler.s_timestep);

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
