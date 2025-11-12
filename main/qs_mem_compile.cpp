/*
    author: Suhas Vittal
    date:   23 September 2025
*/

#include "argparse.h"
#include "generic_io.h"
#include "instruction.h"
#include "compiler/memopt.h"

#include <chrono>
#include <iomanip>

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

    bool validate;

    ARGPARSE()
        .required("input-file", "the trace file (with no memory instructions) to compile", input_trace_file)
        .required("output-file", "the output trace file path", output_trace_file)
        .optional("-i", "--inst-limit", "number of instructions to compile", inst_limit, std::numeric_limits<int64_t>::max())
        .optional("-c", "--cmp-count", "number of surface codes within compute to assume", cmp_count, 4)
        .optional("-pp", "--print-progress", "print progress frequency", print_progress_freq, 100000)
        .optional("-e", "--emit-impl", "emit implementation", emit_impl_id, EMIT_IMPL_ID_VISZLAI)
        .optional("", "--validate", "validate the schedule", validate, false)
        .parse(argc, argv);

    MEMOPT::EMIT_IMPL_ID emit_impl = static_cast<MEMOPT::EMIT_IMPL_ID>(emit_impl_id);

    // read the file:
    generic_strm_type istrm, ostrm;

    generic_strm_open(istrm, input_trace_file, "rb");
    generic_strm_open(ostrm, output_trace_file, "wb");

    // Start timing the compilation
    auto compile_start = std::chrono::high_resolution_clock::now();

    MEMOPT compiler(cmp_count, emit_impl, print_progress_freq);
    compiler.run(istrm, ostrm, inst_limit);

    // End timing the compilation
    auto compile_end = std::chrono::high_resolution_clock::now();

    generic_strm_close(istrm);
    generic_strm_close(ostrm);

    // Calculate compilation time in seconds
    auto compile_duration = std::chrono::duration_cast<std::chrono::microseconds>(compile_end - compile_start);
    double compile_time_seconds = compile_duration.count() / 1000000.0;

    double compute_intensity = static_cast<double>(compiler.s_unrolled_inst_done) 
                                    / static_cast<double>(compiler.s_memory_instructions_added);
    double mean_rref_interval = static_cast<double>(compiler.s_total_rref) / static_cast<double>(compiler.s_num_rref);

    uint64_t near_rref = std::reduce(compiler.s_rref_histogram.begin(), 
                                        compiler.s_rref_histogram.begin()+4, 
                                        uint64_t{0});

    print_stat_line(std::cout, "INST_DONE", compiler.s_inst_done);
    print_stat_line(std::cout, "UNROLLED_INST_DONE", compiler.s_unrolled_inst_done);
    print_stat_line(std::cout, "MEMORY_INSTRUCTIONS", compiler.s_memory_instructions_added);
    print_stat_line(std::cout, "MEMORY_PREFETCHES", compiler.s_memory_prefetches_added);
    print_stat_line(std::cout, "EMISSION_CALLS", compiler.s_emission_calls);
    print_stat_line(std::cout, "TOTAL_TIMESTEPS", compiler.s_timestep);

    print_stat_line(std::cout, "COMPILE_TIME", compile_time_seconds);
    print_stat_line(std::cout, "COMPUTE_INTENSITY", compute_intensity);

    print_stat_line(std::cout, "MEAN_RREF_INTERVAL", mean_rref_interval);
    print_stat_line(std::cout, "NEAR_IMMEDIATE_RREF", near_rref);

    std::cout << "RREF_HISTOGRAM\n";
    for (size_t i = 0; i < compiler.s_rref_histogram.size(); i++)
    {
        print_stat_line(std::cout, "\tRREF=" + std::to_string(i+1), compiler.s_rref_histogram[i]);
    }
    
    // validate schedule:
    if (validate)
    {
        generic_strm_type gt_istrm, test_istrm;
        generic_strm_open(gt_istrm, input_trace_file, "rb");
        generic_strm_open(test_istrm, output_trace_file, "rb");

        if (validate_schedule(gt_istrm, test_istrm, cmp_count))
            std::cout << "SCHEDULE VALIDATED\n";
        else
            std::cout << "SCHEDULE INVALID\n";
    }

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
