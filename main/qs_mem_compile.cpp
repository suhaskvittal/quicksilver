/*
    author: Suhas Vittal
    date:   23 September 2025
*/

#include "argparse.h"
#include "generic_io.h"
#include "instruction.h"
#include "memory/compiler.h"

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char** argv)
{
    std::string input_trace_file;
    std::string output_trace_file;
    int64_t inst_limit;
    int64_t cmp_count;
    int64_t print_progress_freq;
    bool enable_rotation_directed_prefetch;

    ARGPARSE()
        .required("input-file", "the trace file (with no memory instructions) to compile", input_trace_file)
        .required("output-file", "the output trace file path", output_trace_file)
        .optional("-i", "--inst-limit", "number of instructions to compile", inst_limit, std::numeric_limits<uint64_t>::max())
        .optional("-c", "--cmp-count", "number of surface codes within compute to assume", cmp_count, 4)
        .optional("", "--rotation-directed-pf", "enable rotation-directed prefetch", enable_rotation_directed_prefetch, false)
        .optional("-pp", "--print-progress", "print progress frequency", print_progress_freq, 100000)
        .parse(argc, argv);

    // read the file:
    generic_strm_type istrm, ostrm;

    generic_strm_open(istrm, input_trace_file, "rb");
    generic_strm_open(ostrm, output_trace_file, "wb");

    MEMORY_COMPILER compiler(cmp_count, enable_rotation_directed_prefetch, print_progress_freq);
    compiler.run(istrm, ostrm, inst_limit);

    generic_strm_close(istrm);
    generic_strm_close(ostrm);

    std::cout << "[ MEMORY_COMPILER ] done -- inst completed: " << compiler.s_inst_done
                << ", memory instructions added: " << compiler.s_memory_instructions_added
                << ", memory prefetches added: " << compiler.s_memory_prefetches_added 
                << "\n";

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////