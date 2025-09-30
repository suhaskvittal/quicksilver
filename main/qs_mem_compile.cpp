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

    double mean_lifetime = static_cast<double>(compiler.s_total_lifetime_in_working_set) / static_cast<double>(compiler.s_num_lifetimes_recorded);

    std::cout << "done -- inst completed: " << compiler.s_inst_done
                << ", memory instructions added: " << compiler.s_memory_instructions_added
                << ", memory prefetches added: " << compiler.s_memory_prefetches_added 
                << ", unused bandwidth: " << compiler.s_unused_bandwidth
                << ", emission calls: " << compiler.s_emission_calls
                << ", mean lifetime: " << mean_lifetime
                << ", total timesteps: " << compiler.s_timestep
                << "\n";

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////