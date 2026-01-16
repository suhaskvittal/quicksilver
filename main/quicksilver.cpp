/*
 *  author: Suhas Vittal
 *  date:   16 January 2026
 * */

#include "sim.h"
#include "sim/configuration/resource_estimation.h"
#include "sim/configuration/allocator.h"
#include "sim/compute_subsystem.h"
#include "sim/memory_subsystem.h"
#include "sim/factory.h"

#include "compiler/memory_scheduler.h"
#include "compiler/memory_scheduler/impl.h"

#include "argparse.h"

#include <sys/stat.h>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace 
{

constexpr size_t COMPUTE_CODE_DISTANCE{21};
constexpr size_t MEMORY_CODE_DISTANCE{24};

constexpr size_t MEMORY_BLOCK_PHYSICAL_QUBITS = 
    sim::configuration::bivariate_bicycle_code_physical_qubit_count(MEMORY_CODE_DISTANCE);

constexpr size_t MEMORY_BLOCK_CAPACITY = 
    sim::configuration::bivariate_bicycle_code_logical_qubit_count(MEMORY_CODE_DISTANCE);

std::vector<std::string> split_trace_string(std::string);

/*
 * Compiles the given trace by performing memory access scheduler. The `trace`
 * reference is then overwritten with the new trace.
 * */
void jit_compile(std::string& trace, int64_t inst_sim, int64_t active_set_capacity);

/*
 * Retrieves the number of qubits for the given trace:
 * */
size_t get_number_of_qubits(std::string_view);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    std::string trace_string;
    int64_t     inst_sim;

    int64_t print_progress;
    int64_t ratemode;
    bool    jit;

    int64_t concurrent_clients;
    int64_t compute_local_memory_capacity;
    int64_t compute_syndrome_extraction_round_time_ns;

    int64_t memory_syndrome_extraction_round_time_ns;

    int64_t factory_physical_qubit_budget;

    ARGPARSE()
        .required("trace string", "Path to trace file (if single file or ratemode > 1), or paths separated by `;`", trace_string)
        .required("simulation instructions", "Number of instructions to simulate (for each workload)", inst_sim)

        .optional("-pp", "--print-progress", "Progress print frequency (in compute cycles)", print_progress, 0)
        .optional("", "--ratemode", "If a single trace file is provided, then number of clients using that file", ratemode, 1)
        .optional("-jit", "", "Just-in-time compilation for an input source file", jit, false)

        .optional("-c", "--concurrent-clients", "Number of active concurrent clients", concurrent_clients, 1)
        .optional("-a", "--compute-local-memory-capacity", "Number of active qubits in the compute subsystem's local memory", 
                      compute_local_memory_capacity, 12)
        .optional("", "--compute-syndrome-extraction-round-time-ns", 
                      "Syndrome extraction round latency for surface code (in nanoseconds)", 
                      compute_syndrome_extraction_round_time_ns, 1200)

        .optional("", "--memory-syndrome-extraction-round-time-ns", 
                      "Syndrome extraction round latency for the QLDPC code (in nanoseconds)", 
                      memory_syndrome_extraction_round_time_ns, 1300)

        .optional("-f", "--factory-physical-qubit-budget", "Number of physical qubits allocated to factory allocator", 
                      factory_physical_qubit_budget, 50000)

        .parse(argc, argv);

    /* Parse trace string and do jit compilation if neeeded */

    auto traces = split_trace_string(trace_string);
    if (ratemode > 1 && traces.size() > 1)
        std::cerr << "cannot have multiple input traces if ratemode > 1" << _die{};

    if (jit)
        for (std::string& trace : traces)
            jit_compile(trace, inst_sim, compute_local_memory_capacity);

    /* initialize magic state factories */

    sim::configuration::FACTORY_SPECIFICATION l1_spec
    {
        .is_cultivation=true,
        .syndrome_extraction_round_time_ns=compute_syndrome_extraction_round_time_ns,
        .buffer_capacity=1,
        .output_error_rate=1e-6,
        .escape_distance=13,
        .round_length=25,
        .probability_of_success=0.2
    };

    sim::configuration::FACTORY_SPECIFICATION l2_spec
    {
        .is_cultivation=false,
        .syndrome_extraction_round_time_ns=compute_syndrome_extraction_round_time_ns,
        .buffer_capacity=4,
        .output_error_rate=1e-12,
        .dx=25,
        .dz=11,
        .dm=11,
        .input_count=4,
        .output_count=1,
        .rotations=11
    };

    sim::configuration::FACTORY_ALLOCATION alloc =
        sim::configuration::throughput_aware_factory_allocation(factory_physical_qubit_budget, l1_spec, l2_spec);

    /* initialize memory subsystem */

    // determine number of qubits for each trace:
    size_t main_memory_qubits = std::transform_reduce(traces.begin(), traces.end(), size_t{0},
                                                std::plus<size_t>{},
                                                [] (const std::string& t) { return get_number_of_qubits(t); });
    main_memory_qubits -= compute_local_memory_capacity;
    const size_t num_blocks = main_memory_qubits == 0 ? 0 : (main_memory_qubits-1) / MEMORY_BLOCK_CAPACITY + 1;
    const double m_freq_khz = sim::compute_freq_khz(MEMORY_CODE_DISTANCE * memory_syndrome_extraction_round_time_ns);
    std::vector<sim::STORAGE*> memory_blocks(num_blocks);
    for (size_t i = 0; i < num_blocks; i++)
    {
        memory_blocks[i] = new sim::STORAGE{m_freq_khz, 
                                            MEMORY_BLOCK_PHYSICAL_QUBITS,
                                            MEMORY_BLOCK_CAPACITY,
                                            MEMORY_CODE_DISTANCE,
                                            2,
                                            1};
    }

    sim::MEMORY_SUBSYSTEM* memory_subsystem = new sim::MEMORY_SUBSYSTEM(std::move(memory_blocks));

    /* initialize compute subsystem */

    const double c_freq_khz = sim::compute_freq_khz(COMPUTE_CODE_DISTANCE * compute_syndrome_extraction_round_time_ns);
    sim::COMPUTE_SUBSYSTEM* compute_subsystem = new sim::COMPUTE_SUBSYSTEM{c_freq_khz,
                                                                             traces,
                                                                             compute_local_memory_capacity,
                                                                             concurrent_clients,
                                                                             inst_sim,
                                                                             alloc.second_level,
                                                                             memory_subsystem};

    /* initialize simulation */

    std::vector<sim::OPERABLE*> all_operables;
    all_operables.push_back(compute_subsystem);
    std::copy(memory_subsystem->storages().begin(), memory_subsystem->storages().end(), std::back_inserter(all_operables));
    std::copy(alloc.first_level.begin(), alloc.first_level.end(), std::back_inserter(all_operables));
    std::copy(alloc.second_level.begin(), alloc.second_level.end(), std::back_inserter(all_operables));
    sim::coordinate_clock_scale(all_operables);

    std::cout << "simulation parameters:"
                << "\n\tqubits in local memory = " << compute_local_memory_capacity
                << "\n\tqubits in main memory (blocks) = " << main_memory_qubits << " (" << num_blocks << ")"
                << "\n\tL1 factories = " << alloc.first_level.size()
                << "\n\tL2 factories = " << alloc.second_level.size()
                << "\n";

    /* run simulation */

    uint64_t last_print_cycle{0};
    do
    {
        if (print_progress > 0)
        {
            bool do_print = (compute_subsystem->current_cycle() % print_progress == 0)
                            && compute_subsystem->current_cycle() > last_print_cycle;
            if (do_print)
            {
                compute_subsystem->print_progress(std::cout);
                last_print_cycle = compute_subsystem->current_cycle();
            }
        }

        for (auto* f : alloc.first_level)
            f->tick();
        for (auto* f : alloc.second_level)
            f->tick();

        memory_subsystem->tick();
        compute_subsystem->tick();
    }
    while (!compute_subsystem->done());

    /* print stats */
    for (auto* c : compute_subsystem->clients())
        sim::print_client_stats(std::cout, compute_subsystem, c);

    /* cleanup simulation */

    delete compute_subsystem;
    delete memory_subsystem;
    for (auto* f : alloc.first_level)
        delete f;
    for (auto* f : alloc.second_level)
        delete f;

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<std::string>
split_trace_string(std::string s)
{
    std::vector<std::string> result;
    size_t start = 0;
    size_t end = s.find(';');

    while (end != std::string::npos) {
        result.push_back(s.substr(start, end - start));
        start = end + 1;
        end = s.find(';', start);
    }

    // Add the last token (or the entire string if no semicolon was found)
    result.push_back(s.substr(start));

    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
jit_compile(std::string& trace, int64_t inst_sim, int64_t active_set_capacity)
{
    std::string trace_dir = trace.substr(0, trace.find_last_of("/\\") + 1) + "jit/";
    std::string trace_filename = trace.substr(trace.find_last_of("/\\") + 1);

    mkdir(trace_dir.c_str(), 0777);

    auto ext_it = trace_filename.find(".gz");
    if (ext_it == std::string::npos)
        ext_it = trace_filename.find(".xz");
    std::string base_name = trace_filename.substr(0, ext_it);
    std::string active_set_capacity_str = std::to_string(active_set_capacity);
    std::string inst_str = std::to_string(inst_sim/1'000'000) + "M";

    std::string compiled_trace = trace_dir + base_name + "_a" + active_set_capacity_str + "_" + inst_str + ".gz";
    
    std::cout << "********* (jit) running memory access scheduler for " << trace 
                << " -> " << compiled_trace << " *********\n";

    generic_strm_type istrm, ostrm;
    generic_strm_open(istrm, trace, "rb");
    generic_strm_open(ostrm, compiled_trace, "wb");

    compile::memory_scheduler::config_type conf;
    conf.active_set_capacity = active_set_capacity;
    conf.inst_compile_limit = static_cast<int64_t>(1.2 * inst_sim);
    conf.print_progress_frequency = 0;
    conf.dag_inst_capacity = 100000;
    conf.hint_lookahead_depth = 256;

    run(ostrm, istrm, compile::memory_scheduler::hint, conf);

    generic_strm_close(istrm);
    generic_strm_close(ostrm);

    trace = compiled_trace;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
get_number_of_qubits(std::string_view trace)
{
    generic_strm_type istrm;
    generic_strm_open(istrm, std::string{trace}, "rb");
    uint32_t num_qubits;
    generic_strm_read(istrm, &num_qubits, 4);
    return num_qubits;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
