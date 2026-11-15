/*
 *  author: Suhas Vittal
 *  date:   5 January 2026
 * */

#include "argparse/argparse.h"
#include "generic_io.h"
#include "compiler/pass/memory_scheduler.h"
#include "compiler/pass/memory_scheduler/impl.h"

#include <chrono>
#include <iomanip>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    std::string                            input_trace_file;
    std::string                            output_trace_file;
    std::string                            memory_stream_file;
    compiler::pass::memory_scheduler::Config conf;
    int64_t                                scheduler_impl_id;

    ARGPARSE()
        .required("input-file", "The trace file (without memory instructions) to compile", input_trace_file)
        .required("output-file", "The output trace file path", output_trace_file)
        .optional("-ms", "--memory-stream", "Path to write the binary memory stream, for use with "
                                             "memory_stream_visualizer/ (plain file only, no .gz/.xz)",
                                             memory_stream_file, "")
        .optional("-c", "--active-set-capacity", "Number of program qubits in the active set", conf.active_set_capacity, 12)
        .optional("-i", "--inst-limit", "Number of instructions to compile", conf.inst_compile_limit, 15'000'000)
        .optional("-pp", "--print-progress", "Print progress frequency (#inst)", conf.print_progress_frequency, 1'000'000)
        .optional("", "--dag-capacity", "DAG instruction capacity", conf.dag_inst_capacity, 8192)
        .optional("-v", "--verbose", "Verbose flag", conf.verbose, false)
        .optional("-s", "--scheduler", "Scheduler ID (0 = EIF, 1 = HINT)", scheduler_impl_id, 0)

        /* EIF PARAMETERS START HERE */
        .optional("", "--eif-lookahead-depth", "EIF Lookahead Depth (layers)", conf.eif_lookahead_depth, 0)

        /* HINT PARAMETERS START HERE */
        .optional("", "--hint-lookahead-depth", "HINT Lookahead Depth (layers)", conf.hint_lookahead_depth, 16)
        .optional("", "--hint-use-coalescing", "Enable HINT CST Coalescing", conf.hint_use_complex_selection, false)
        .optional("", "--hint-use-nonarbitrary-victim-selection", "Lookahead for HINT victim selection", conf.hint_use_nonarbitrary_victim_selection, false)

        .parse(argc, argv);

    // The memory-stream format is seekable-by-offset, so it must be a plain
    // (uncompressed) file -- `generic_strm_open` picks compression by
    // substring match anywhere in the path, and writing to `.xz` throws
    // mid-run (see `generic_strm_write`, `src/generic_io.cpp`).
    if (memory_stream_file.find(".xz") != std::string::npos
        || memory_stream_file.find(".gz") != std::string::npos)
    {
        std::cerr << "--memory-stream: path must not contain \".xz\" or \".gz\" -- "
                     "the memory stream format requires a plain, seekable file" << _die{};
    }

    generic_strm_type istrm, ostrm, mstrm;
    generic_strm_open(istrm, input_trace_file, "rb");
    generic_strm_open(ostrm, output_trace_file, "wb");

    const bool write_memory_stream = !memory_stream_file.empty();
    if (write_memory_stream)
        generic_strm_open(mstrm, memory_stream_file, "wb");

    const char* scheduler_name = (scheduler_impl_id == 0) ? "EIF" : (scheduler_impl_id == 1) ? "HINT" : "UNKNOWN";
    compiler::pass::memory_scheduler::MemoryStreamOptions mstream_opts{
        &mstrm, static_cast<uint32_t>(scheduler_impl_id), scheduler_name};
    const compiler::pass::memory_scheduler::MemoryStreamOptions* mstream_opts_ptr =
        write_memory_stream ? &mstream_opts : nullptr;

    auto compile_start = std::chrono::high_resolution_clock::now();
    compiler::pass::memory_scheduler::Stats stats = [&]
    {
        if (scheduler_impl_id == 0)
            return compiler::pass::memory_scheduler::run(ostrm, istrm, compiler::pass::memory_scheduler::eif, conf, mstream_opts_ptr);
        else if (scheduler_impl_id == 1)
            return compiler::pass::memory_scheduler::run(ostrm, istrm, compiler::pass::memory_scheduler::hint, conf, mstream_opts_ptr);
        std::cerr << "unknown memory scheduler id: " << scheduler_impl_id << _die{};
        return compiler::pass::memory_scheduler::Stats{};
    }();
    auto compile_end = std::chrono::high_resolution_clock::now();

    generic_strm_close(istrm);
    generic_strm_close(ostrm);
    if (write_memory_stream)
        generic_strm_close(mstrm);

    // print stats:
    auto compile_duration = std::chrono::duration_cast<std::chrono::microseconds>(compile_end - compile_start);
    double compile_time_seconds = compile_duration.count() / 1000000.0;

    double compute_intensity = fpdiv(stats.unrolled_inst_done, stats.memory_accesses);

    print_stat_line(std::cout, "INST_DONE", stats.unrolled_inst_done);
    print_stat_line(std::cout, "MEMORY_ACCESSES", stats.memory_accesses);
    print_stat_line(std::cout, "SCHEDULING_EPOCHS", stats.scheduler_epochs);
    print_stat_line(std::cout, "COMPUTE_INTENSITY", compute_intensity);
    print_stat_line(std::cout, "COMPILATION_TIME_SECONDS", compile_time_seconds);
    stats.unused_bandwidth.dump(std::cout);
    stats.memory_level_parallelism.dump(std::cout);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
