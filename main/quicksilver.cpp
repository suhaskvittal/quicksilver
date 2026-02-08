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
    std::string regime;

    int64_t concurrent_clients;
    int64_t compute_local_memory_capacity;
    int64_t compute_syndrome_extraction_round_time_ns;

    int64_t memory_syndrome_extraction_round_time_ns;

    int64_t factory_l2_buffer_capacity;
    int64_t factory_physical_qubit_budget;

    sim::compute_extended_config conf;

    bool bsol_sync_to_l2_factory;

    ARGPARSE()
        .required("trace string", "Path to trace file (if single file or ratemode > 1), or paths separated by `;`", trace_string)
        .required("simulation instructions", "Number of instructions to simulate (for each workload)", inst_sim)

        .optional("-pp", "--print-progress", "Progress print frequency (in compute cycles)", print_progress, 0)
        .optional("", "--ratemode", "If a single trace file is provided, then number of clients using that file", ratemode, 1)
        .optional("-jit", "", "Just-in-time compilation for an input source file", jit, false)
        .optional("", "--regime", 
                    "Choose one of: M, G, T (megaquop, gigaqoup, terquop). This affects code distance + factory allocation",
                    regime, "T")

        .optional("-c", "--concurrent-clients", "Number of active concurrent clients", concurrent_clients, 1)
        .optional("-a", "--compute-local-memory-capacity", "Number of active qubits in the compute subsystem's local memory", 
                      compute_local_memory_capacity, 12)
        .optional("", "--compute-syndrome-extraction-round-time-ns", 
                      "Syndrome extraction round latency for surface code (in nanoseconds)", 
                      compute_syndrome_extraction_round_time_ns, 1200)

        .optional("-ttpl", "--t-teleport-limit", "Max number of T gate teleportations after initial T gate", sim::GL_T_GATE_TELEPORTATION_MAX, 0)
        .optional("", "--enable-t-autocorrect", "Use auto correction when applying T gates", sim::GL_T_GATE_DO_AUTOCORRECT, false)


        .optional("-rpc", "--rpc", "Enable rotation precomputation", conf.rpc_enabled, false)
        .optional("", "--rpc-ttp-always", "Enable T teleportation always for rotation subsystem", sim::GL_RPC_RS_ALWAYS_USE_TELEPORTATION, false)
        .optional("", "--rpc-capacity", "Amount of rotation precomputation storage", conf.rpc_capacity, 2)
        .optional("", "--rpc-watermark", "Watermark for rotation precomputation", conf.rpc_watermark, 0.5)

        .optional("", "--memory-syndrome-extraction-round-time-ns", 
                      "Syndrome extraction round latency for the QLDPC code (in nanoseconds)", 
                      memory_syndrome_extraction_round_time_ns, 1300)

        .optional("", "--factory-l2-buffer-capacity", "Number of magic states stored in an L2 factory buffer",
                      factory_l2_buffer_capacity, 4)
        .optional("-f", "--factory-physical-qubit-budget", "Number of physical qubits allocated to factory allocator", 
                      factory_physical_qubit_budget, 50000)

        /*
         * These are parameters for analyzing *where* T bandwidth goes, since applications cannot saturate all of
         * it
         * */
        .optional("", "--bsol-elide-cliffords", "BW SoL: Elide Clifford gates", sim::GL_ELIDE_CLIFFORDS, false)
        .optional("", "--bsol-zero-latency-t", "BW SoL: Zero latency T gates", sim::GL_ZERO_LATENCY_T_GATES, false)
        .optional("", "--bsol-sync", "BW SoL: sync compute to L2 factory", bsol_sync_to_l2_factory, false)

        .parse(argc, argv);

    GL_USE_RPC_ISA = 1;

    /* Parse trace string and do jit compilation if neeeded */

    auto traces = split_trace_string(trace_string);
    if (ratemode > 1 && traces.size() > 1)
        std::cerr << "cannot have multiple input traces if ratemode > 1" << _die{};

    if (jit)
        for (std::string& trace : traces)
            jit_compile(trace, inst_sim, compute_local_memory_capacity);

    if (ratemode > 1)
    {
        std::string trace{traces[0]};
        traces.resize(ratemode);
        std::fill(traces.begin(), traces.end(), trace);
    }

    /* initialize magic state factories */

    sim::configuration::FACTORY_SPECIFICATION l1_spec
    {
        .is_cultivation=true,
        .syndrome_extraction_round_time_ns=compute_syndrome_extraction_round_time_ns,
        .buffer_capacity=1,
        .output_error_rate=1e-6,
        .escape_distance=13,
        .round_length=18,
        .probability_of_success=0.2
    };

    sim::configuration::FACTORY_SPECIFICATION l2_spec
    {
        .is_cultivation=false,
        .syndrome_extraction_round_time_ns=compute_syndrome_extraction_round_time_ns,
        .buffer_capacity=factory_l2_buffer_capacity,
        .output_error_rate=1e-12,
        .dx=25,
        .dz=11,
        .dm=11,
        .input_count=4,
        .output_count=1,
        .rotations=11
    };

    // from `regime`, set parameters:
    size_t compute_code_distance,
           memory_code_distance;
    if (regime == "M")  // 1e-6 error rate
    {
        compute_code_distance = 11;
        memory_code_distance = 12;
    }
    else if (regime == "G")
    {
        compute_code_distance = 17;
        memory_code_distance = 18;
        // modify `l1_spec` to use d = 5 color code cultivation
        l1_spec.output_error_rate = 1e-8;
        l1_spec.escape_distance = 15;
        l1_spec.round_length = 25;
        l1_spec.probability_of_success = 0.02;
    }
    else
    {
        compute_code_distance = 21;
        memory_code_distance = 24;
    }
    conf.rpc_freq_khz = sim::compute_freq_khz((compute_code_distance-4) * compute_syndrome_extraction_round_time_ns);

    const size_t memory_block_physical_qubits = 
        sim::configuration::bivariate_bicycle_code_physical_qubit_count(memory_code_distance);
    const size_t memory_block_capacity =
        sim::configuration::bivariate_bicycle_code_logical_qubit_count(memory_code_distance);

    sim::configuration::FACTORY_ALLOCATION alloc;
    if (regime == "T")
        alloc = sim::configuration::throughput_aware_factory_allocation(factory_physical_qubit_budget, l1_spec, l2_spec);
    else
        alloc = sim::configuration::l1_factory_allocation(factory_physical_qubit_budget, l1_spec);

    /* initialize memory subsystem */

    // determine number of qubits for each trace:
    size_t main_memory_qubits = std::transform_reduce(traces.begin(), traces.end(), size_t{0},
                                                std::plus<size_t>{},
                                                [] (const std::string& t) { return get_number_of_qubits(t); });
    main_memory_qubits -= compute_local_memory_capacity;
    const size_t num_blocks = main_memory_qubits == 0 ? 0 : (main_memory_qubits-1) / memory_block_capacity + 1;
    const double m_freq_khz = sim::compute_freq_khz(memory_code_distance * memory_syndrome_extraction_round_time_ns);
    std::vector<sim::STORAGE*> memory_blocks(num_blocks);
    for (size_t i = 0; i < num_blocks; i++)
    {
        memory_blocks[i] = new sim::STORAGE{m_freq_khz, 
                                            memory_block_physical_qubits,
                                            memory_block_capacity,
                                            memory_code_distance,
                                            1, // num adapters
                                            2, // load latency
                                            1 // store latency
        };
    }

    sim::MEMORY_SUBSYSTEM* memory_subsystem = new sim::MEMORY_SUBSYSTEM(std::move(memory_blocks));

    /* initialize compute subsystem */

    double c_freq_khz;
    if (bsol_sync_to_l2_factory)
        c_freq_khz = sim::compute_freq_khz(l2_spec.dm * compute_syndrome_extraction_round_time_ns);
    else
        c_freq_khz = sim::compute_freq_khz(compute_code_distance * compute_syndrome_extraction_round_time_ns);

    sim::COMPUTE_SUBSYSTEM* compute_subsystem = new sim::COMPUTE_SUBSYSTEM{c_freq_khz,
                                                                             traces,
                                                                             compute_local_memory_capacity,
                                                                             concurrent_clients,
                                                                             inst_sim,
                                                                             alloc.second_level.empty() ? alloc.first_level : alloc.second_level,
                                                                             memory_subsystem,
                                                                             conf};

    /* initialize simulation */

    std::vector<sim::OPERABLE*> all_operables;
    all_operables.push_back(compute_subsystem);
    std::copy(memory_subsystem->storages().begin(), memory_subsystem->storages().end(), std::back_inserter(all_operables));
    std::copy(alloc.first_level.begin(), alloc.first_level.end(), std::back_inserter(all_operables));
    std::copy(alloc.second_level.begin(), alloc.second_level.end(), std::back_inserter(all_operables));

    if (compute_subsystem->is_rpc_enabled())
        all_operables.push_back(compute_subsystem->rotation_subsystem());

    sim::coordinate_clock_scale(all_operables);

    std::cout << "simulation parameters:"
                << "\n\tqubits in local memory = " << compute_local_memory_capacity
                << "\n\tqubits in main memory (blocks) = " << main_memory_qubits << " (" << num_blocks << ")"
                << "\n\tL1 factories = " << alloc.first_level.size()
                << "\n\tL2 factories = " << alloc.second_level.size()
                << "\n";

    /* run simulation */

    sim::GL_SIM_WALL_START = std::chrono::steady_clock::now();
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
        
        for (auto* x : all_operables)
            x->tick();
    }
    while (!compute_subsystem->done());

    /* print stats */

    size_t compute_physical_qubits = sim::configuration::surface_code_physical_qubit_count(compute_code_distance)
                                            * compute_local_memory_capacity;
    size_t memory_physical_qubits = std::transform_reduce(memory_subsystem->storages().begin(),
                                                            memory_subsystem->storages().end(),
                                                            size_t{0},
                                                            std::plus<size_t>{},
                                                            [] (auto* s) { return s->physical_qubit_count; });
    size_t factory_physical_qubits = alloc.physical_qubit_count;
    double t_throughput_per_cycle = estimate_throughput_of_allocation(alloc, true)
                                        * (1.0/(1e3*compute_subsystem->freq_khz));


    sim::print_compute_subsystem_stats(std::cout, compute_subsystem);

    sim::print_stats_for_factories(std::cout, "L1_FACTORY", alloc.first_level);
    sim::print_stats_for_factories(std::cout, "L2_FACTORY", alloc.second_level);

    print_stat_line(std::cout, "COMPUTE_PHYSICAL_QUBITS", compute_physical_qubits);
    print_stat_line(std::cout, "MEMORY_PHYSICAL_QUBITS", memory_physical_qubits);
    print_stat_line(std::cout, "FACTORY_PHYSICAL_QUBITS", factory_physical_qubits);
    print_stat_line(std::cout, "T_BANDWIDTH_MAX_PER_CYCLE", t_throughput_per_cycle);
    print_stat_line(std::cout, "T_BANDWIDTH_MAX_PER_S", estimate_throughput_of_allocation(alloc, true));

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
    constexpr auto MEMORY_ACCESS_SCHEDULER{compile::memory_scheduler::eif};

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
    conf.inst_compile_limit = static_cast<int64_t>(5 * inst_sim);
    conf.print_progress_frequency = 0;
    conf.dag_inst_capacity = 100000;
    conf.hint_lookahead_depth = 256;

    run(ostrm, istrm, MEMORY_ACCESS_SCHEDULER, conf);

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
