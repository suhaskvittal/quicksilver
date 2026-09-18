/*
 *  author: Suhas Vittal
 *  date:   16 January 2026
 * */

#include "sim.h"
#include "sim/configuration/allocator.h"
#include "sim/configuration/resource_estimation.h"
#include "sim/compute_subsystem.h"
#include "sim/driver.h"
#include "sim/memory/bivariate_bicycle.h"
#include "sim/memory/remote_memory.h"
#include "sim/memory_level.h"
#include "sim/production.h"

#include "compiler/pass/memory_scheduler.h"
#include "compiler/pass/memory_scheduler/impl.h"

#include "argparse/argparse.h"

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

/*
 * Returns the code distance for the given error-rate regime
 * */
size_t get_compute_code_distance(std::string_view);
size_t get_memory_code_distance(std::string_view);

/*
 * Gets the default production specifications for magic state factories (parametrized by
 * values that can be extended by the user).
 * */
std::vector<sim::ProducerQuantum> get_default_factory_specifications(std::string_view regime,
                                                                      int64_t compute_cycle_time_ns,
                                                                      int64_t ll_buffer_capacity);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    sim::GL_RNG.seed(0);

    std::string trace_string;
    int64_t     inst_sim;

    int64_t print_progress;
    int64_t ratemode;
    int64_t skip_threshold;
    bool    skip_verbose;
    bool    jit;
    std::string regime;
    int64_t total_inst;

    int64_t concurrent_clients;
    int64_t compute_local_memory_capacity;
    int64_t compute_cycle_time_ns;

    int64_t memory_cycle_time_ns;

    int64_t factory_ll_buffer_capacity;
    int64_t factory_physical_qubit_budget;

    bool    remote_memory;
    int64_t remote_memory_epr_budget;

    int64_t oc_base_ro_latency;
    double oc_ro_latency_fraction;

    ARGPARSE()
        .required("trace string", "Path to trace file (if single file or ratemode > 1), or paths separated by `;`", trace_string)
        .required("simulation instructions", "Number of instructions to simulate (for each workload)", inst_sim)

        .optional("-pp", "--print-progress", "Progress print frequency (in compute cycles)", print_progress, 0)
        .optional("", "--ratemode", "If a single trace file is provided, then number of clients using that file", ratemode, 1)
        .optional("-skip", "--skip-threshold", "Number of cycles without progress before skipping cycles", skip_threshold, 100)
        .optional("", "--skip-verbose", "Skip verbose", skip_verbose, false)
        .optional("", "--max-cycles-no-progress",
                    "Maximum consecutive cycles any single component may spend with no progress before the "
                    "simulation declares a deadlock and aborts",
                    sim::GL_MAX_CYCLES_WITH_NO_PROGRESS, 1'000'000)
        .optional("-jit", "", "Just-in-time compilation for an input source file", jit, false)

        .optional("", "--regime", 
                    "Choose one of: M, G, T (megaquop, gigaqoup, terquop). This affects code distance + factory allocation",
                    regime, "T")
        .optional("", "--total-program-instructions", 
                    "Total instructions in the program. Used for resource estimates. Does not affect performance.",
                    total_inst, 1'000'000'000ll)

        .optional("-c", "--concurrent-clients", "Number of active concurrent clients", concurrent_clients, 1)
        .optional("-a", "--compute-local-memory-capacity", "Number of active qubits in the compute subsystem's local memory", 
                      compute_local_memory_capacity, 12)
        .optional("", "--compute-cycle-time-ns", 
                      "Syndrome extraction round latency for surface code (in nanoseconds)", 
                      compute_cycle_time_ns, 1200)

        .optional("", "--reaction-time", "Control reaction time (in compute cycles)", sim::GL_REACTION_TIME, 10)
        .optional("", "--rltp-degree", "Reaction-limited T teleportation degree", sim::GL_RLTP_DEGREE, 0)

        .optional("-rdr", "", "Enable rotation directed runahead", GL_USE_RDR_ISA, 0)
        .optional("", "--rdr-capacity", "Number of ancilla", sim::GL_RDR_CAPACITY, 2)
        .optional("", "--rdr-start-layer", "DAG layer to start runahead", sim::GL_RDR_START_LAYER, 2)
        .optional("", "--rdr-lookahead-depth", "Number of DAG layers to search", sim::GL_RDR_LOOKAHEAD_DEPTH, 16)
        .optional("", "--rdr-inst-delta-limit", "Instruction delta limit for runahead", sim::GL_RDR_INST_DELTA_LIMIT, 500)
        .optional("", "--rdr-degree", "Degree of runahead (number of instructions)", sim::GL_RDR_DEGREE, 1)
        .optional("", "--rdr-completion-buffer-capacity", 
                        "Capacity of completion buffer (number of qubits)",
                        sim::GL_RDR_COMPLETION_BUFFER_CAPACITY,
                        4)
        .optional("", "--rdr-fixed-lookahead", "Fix RDR lookahead layers", sim::GL_RDR_FIXED_LOOKAHEAD, false)
        .optional("", "--rdr-cost-scale", "RDR Cost Multiplier", sim::GL_RDR_COST_SCALE, 4.0)
        .optional("", "--rdr-inv-threshold", "RDR invalidation threshold", sim::GL_RDR_INV_THRESHOLD, 0.75)

        .optional("", "--memory-cycle-time-ns", 
                        "Syndrome extraction round latency for the QLDPC code (in nanoseconds)", 
                        memory_cycle_time_ns, 1300)

        .optional("-f", "--factory-physical-qubit-budget", "Number of physical qubits allocated to factory allocator", 
                      factory_physical_qubit_budget, 50000)
        .optional("", "--factory-ll-buffer-capacity", "Number of magic states stored in an last-level factory buffer",
                      factory_ll_buffer_capacity, 2)

        /*
         *
         * */
        .optional("-rm", "--remote-memory", "Operate memory as a remote region", remote_memory, false)
        .optional("", "--remote-memory-epr-budget", "Number of physical qubits for EPR production",
                      remote_memory_epr_budget, 100000)

        /*
         * These are parameters for analyzing *where* T bandwidth goes, since applications cannot saturate all of it
         * */
        .optional("", "--bsol-elide-cliffords", "BW SoL: Elide Clifford gates", sim::GL_ELIDE_CLIFFORDS, false)
        .optional("", "--bsol-zero-latency-t", "BW SoL: Zero latency T gates", sim::GL_ZERO_LATENCY_T_GATES, false)
        .optional("", "--memory-ideal-resources",
                        "Disable all memory-subsystem resource overhead (EPR, adapters, routing); keep only access latency",
                        sim::GL_MEMORY_IDEAL_RESOURCES, false)

        /*
         * These are parameters for reducing readout latency
         * */
        .optional("", "--oc-base-ro-latency", "Manually set ro latency (general)", oc_base_ro_latency, -1)
        .optional("", "--oc-ro-latency-fraction", "Readout latency fraction for OC", oc_ro_latency_fraction, 0.9)

        .parse(argc, argv);

    sim::GL_RDR_ENABLED = (GL_USE_RDR_ISA > 0);

    if (oc_base_ro_latency >= 0)
    {
        compute_cycle_time_ns = 400 + oc_base_ro_latency;
        memory_cycle_time_ns = 600 + oc_base_ro_latency;
    }

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

    // from `regime`, set parameters:
    const size_t compute_code_distance = get_compute_code_distance(regime),
                 memory_code_distance = get_memory_code_distance(regime);

    const size_t memory_block_physical_qubits = sim::configuration::bivariate_bicycle_code_physical_qubit_count(memory_code_distance);
    const size_t memory_block_capacity = sim::configuration::bivariate_bicycle_code_logical_qubit_count(memory_code_distance);

    /* initialize magic state factories */

    const double c_freq_khz = sim::compute_freq_khz(compute_cycle_time_ns);

    auto ms_specs = get_default_factory_specifications(regime, compute_cycle_time_ns, factory_ll_buffer_capacity);
    if (oc_base_ro_latency >= 0)
    {
        // The last production level runs its syndrome extraction on a separate (typically
        // faster) clock. `BulkProducer` runs every level on a single clock (`c_freq_khz`), so
        // fold that clock ratio into the level's cycle count instead.
        const int64_t last_level_cycle_time_ns = static_cast<int64_t>(std::ceil( 400 + oc_ro_latency_fraction*oc_base_ro_latency ));
        const double scale = static_cast<double>(last_level_cycle_time_ns) / static_cast<double>(compute_cycle_time_ns);
        auto& last = ms_specs.back();
        last.cycles_per_production = static_cast<cycle_type>(std::ceil(last.cycles_per_production * scale));
    }
    auto ms_alloc = sim::configuration::throughput_aware_allocation(factory_physical_qubit_budget, ms_specs, c_freq_khz);

    auto* t_producer = new sim::BulkProducer("t_producer", c_freq_khz);
    for (const auto& [quantum, count] : ms_alloc.levels)
        t_producer->add_level(quantum, count);

    /* initialize memory subsystem */

    // determine number of qubits for each trace:
    size_t total_trace_qubits = std::transform_reduce(traces.begin(), traces.end(), size_t{0},
                                                std::plus<size_t>{},
                                                [] (const std::string& t) { return get_number_of_qubits(t); });
    // `compute_local_memory_capacity` may exceed the trace's own qubit count (e.g. small/synthetic
    // traces run with the default local-capacity budget) -- clamp instead of underflowing `size_t`,
    // which used to request a memory subsystem sized to ~2^64 qubits.
    size_t main_memory_qubits = (total_trace_qubits > compute_local_memory_capacity)
                                    ? total_trace_qubits - compute_local_memory_capacity
                                    : 0;
    const double m_freq_khz = sim::compute_freq_khz(memory_cycle_time_ns);

    /* initialize EPR production (only used for remote memory) */

    sim::BulkProducer* epr_producer{nullptr};
    sim::configuration::Allocation epr_alloc{};
    if (remote_memory)
    {
        std::vector<sim::ProducerQuantum> ed_specs{
            sim::ed_protocol_2a(sim::EDImplMethod::DISAGGREGATED_TYPE_A),
            sim::ed_protocol_2b(sim::EDImplMethod::DISAGGREGATED_TYPE_A)};
        epr_alloc = sim::configuration::throughput_aware_allocation(remote_memory_epr_budget, ed_specs, m_freq_khz);

        epr_producer = new sim::BulkProducer("epr_producer", m_freq_khz);
        for (const auto& [quantum, count] : epr_alloc.levels)
            epr_producer->add_level(quantum, count);
    }

    std::vector<sim::MemoryLevel*> memory_subsystem;
    if (main_memory_qubits > 0)
    {
        sim::MemoryLevel* bb_memory;
        if (remote_memory)
        {
            bb_memory = new sim::RemoteMemory<sim::BBMemory>(m_freq_khz,
                                                                main_memory_qubits,
                                                                memory_block_physical_qubits,
                                                                memory_block_capacity,
                                                                memory_code_distance,
                                                                epr_producer);
        }
        else
        {
            bb_memory = new sim::BBMemory(m_freq_khz, 
                                                main_memory_qubits, 
                                                memory_block_physical_qubits, 
                                                memory_block_capacity, 
                                                memory_code_distance);
        }
        memory_subsystem.push_back(bb_memory);
    }

    /* initialize compute subsystem */

    auto* compute_subsystem = new sim::ComputeSubsystem(c_freq_khz,
                                                         compute_code_distance,
                                                         compute_local_memory_capacity,
                                                         t_producer,
                                                         memory_subsystem);

    /* initialize driver */

    sim::Driver* driver = new sim::Driver(traces,
                                            concurrent_clients,
                                            inst_sim,
                                            compute_subsystem,
                                            t_producer,
                                            epr_producer,
                                            memory_subsystem);

    /* initialize simulation */

    std::vector<sim::Operable*> all_operables{driver, compute_subsystem, t_producer};
    if (epr_producer != nullptr)
        all_operables.push_back(epr_producer);
    std::copy(memory_subsystem.begin(), memory_subsystem.end(), std::back_inserter(all_operables));
    sim::coordinate_clock_scale(all_operables);

    /* run simulation */

    sim::GL_SIM_WALL_START = std::chrono::steady_clock::now();
    uint64_t last_print_cycle{0};
    do
    {
        if (print_progress > 0)
        {
            bool do_print = (driver->current_cycle() % print_progress == 0) 
                                && driver->current_cycle() > last_print_cycle;
            if (do_print)
            {
                driver->print_progress(std::cout);
                last_print_cycle = driver->current_cycle();
            }
        }
        
        for (auto* x : all_operables)
            x->tick();

        // check if we should do a skip:
        if (skip_threshold > 0 && driver->cycles_with_no_progress() >= skip_threshold)
        {
            auto skip = driver->skip_to_cycle();
            if (skip.has_value())
            {
                if (skip_verbose)
                {
                    std::cout << "==== ALERT ====\n"
                                << "simulation has skipped to cycle " << *skip << ", was = " << driver->current_cycle() 
                                << ", delta = " << (*skip - driver->current_cycle())
                                << "\n";
                }
                
                uint64_t skip_time_ns = sim::convert_cycles_to_time_ns(*skip, driver->freq_khz);
                sim::fast_forward_all_operables_to_time_ns(all_operables, skip_time_ns);
            }
        }
    }
    while (!driver->done());
    driver->stop_simulation();

    /* print stats */

    sim::print_sim_stats(std::cout, driver);
    if (epr_producer != nullptr)
        sim::print_stats_for_factories(std::cout, "EPR_FACTORY", epr_producer);

    print_stat_line(std::cout, "COMPUTE_CODE_DISTANCE", compute_code_distance);
    print_stat_line(std::cout, "MEMORY_CODE_DISTANCE", memory_code_distance);

    /*
     * Compute physical qubit estimates:
     * */

    const size_t sc_footprint = sim::configuration::surface_code_physical_qubit_count(compute_code_distance);
    size_t program_active_memory_footprint,
           rltp_footprint,
           rdr_storage_overhead,
           rdr_footprint;
    // program active memory overheads: multiply by 1.5x to account for routing overhead (assuming bus)
    program_active_memory_footprint = 1.5 * compute_local_memory_capacity * sc_footprint;
    // RLTP physical qubit overheads:
    rltp_footprint = std::min( (7*sim::GL_RLTP_DEGREE),                         // O(n) method
                                sqr(sim::GL_RLTP_DEGREE)/2 + 3*sim::GL_RLTP_DEGREE  // O(n^2) method
                             ) * sc_footprint;
    // RDR phsyical qubit overheads:
    rdr_storage_overhead = sim::GL_RDR_COMPLETION_BUFFER_CAPACITY > 0
                              ? 1.5 * (sim::GL_RDR_COMPLETION_BUFFER_CAPACITY+2)
                                    * sim::configuration::surface_code_physical_qubit_count(13) // d = 13 for yoked surface code
                              : 0;
    rdr_footprint = sim::GL_RDR_ENABLED
                        ? (1.5*sim::GL_RDR_CAPACITY*sc_footprint + rdr_storage_overhead)
                        : 0;
    const size_t total_compute_footprint = program_active_memory_footprint + rltp_footprint + rdr_footprint;

    std::cout << "COMPUTE_FOOTPRINT\n";
    print_stat_line(std::cout, "    PROGRAM_MEMORY", program_active_memory_footprint);
    print_stat_line(std::cout, "    RLTP_WORKSPACE", rltp_footprint);
    print_stat_line(std::cout, "    RDR_WORKSPACE", rdr_footprint);
    print_stat_line(std::cout, "    TOTAL", total_compute_footprint);

    /*
     * Memory physical qubit overheads:
     * */

    const size_t program_inactive_memory_footprint = std::transform_reduce(
                                                        memory_subsystem.begin(), 
                                                        memory_subsystem.end(),
                                                        size_t{0},
                                                        std::plus<size_t>{},
                                                        [sc_footprint, remote_memory] (const auto* m)
                                                        {
                                                            size_t memory_overhead = m->storage_physical_qubit_count*m->num_blocks;
                                                            size_t routing_overhead;
                                                            if (remote_memory)
                                                                routing_overhead = 0;
                                                            else
                                                                routing_overhead = 0.5*m->num_blocks*sc_footprint;
                                                            return memory_overhead + routing_overhead;
                                                        });
    
    std::cout << "MEMORY_FOOTPRINT\n";
    print_stat_line(std::cout, "    PROGRAM_MEMORY", program_inactive_memory_footprint);

    std::cout << "FACTORY_FOOTPRINT\n";
    print_stat_line(std::cout, "    TOTAL", ms_alloc.physical_qubit_count);

    if (remote_memory)
    {
        std::cout << "EPR_FOOTPRINT\n";
        print_stat_line(std::cout, "    TOTAL", epr_alloc.physical_qubit_count);
    }

    const size_t total_footprint = total_compute_footprint + program_inactive_memory_footprint
                                        + ms_alloc.physical_qubit_count + epr_alloc.physical_qubit_count;
    print_stat_line(std::cout, "TOTAL_FOOTPRINT", total_footprint);

    auto fidelity = driver->application_fidelity(0, total_inst, sim::GL_PHYSICAL_ERROR_RATE);
    std::cout << "FIDELITY\n";
    print_stat_line(std::cout, "    OVERALL", fidelity.overall);
    print_stat_line(std::cout, "    COMPUTE", fidelity.compute);
    print_stat_line(std::cout, "    MEMORY", fidelity.mem);
    print_stat_line(std::cout, "    RDR", fidelity.rdr);

    print_stat_line(std::cout, "T_BANDWIDTH_MAX_PER_S", ms_alloc.estimated_throughput);
    if (remote_memory)
    {
        // calculate L0 ingress rate (physical EPR generation rate)
        auto [l0_q, l0_n] = epr_alloc.levels[0];
        double l0_ingress_rate = l0_n * fpdiv(l0_q.d_input_states, l0_q.cycles_per_production) * (epr_producer->freq_khz * 1e3);

        print_stat_line(std::cout, "LOGICAL_EPR_BANDWIDTH_MAX_PER_S", epr_alloc.estimated_throughput);
        print_stat_line(std::cout, "PHYSICAL_EPR_DISTR_RATE_MAX_PER_S", l0_ingress_rate);
    }
    print_stat_line(std::cout, "SIMULATION_WALLTIME_S", sim::walltime_s());

    /* cleanup simulation */

    delete driver;
    delete compute_subsystem;
    delete t_producer;
    delete epr_producer;
    for (auto* m : memory_subsystem)
        delete m;

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
    constexpr auto MEMORY_ACCESS_SCHEDULER{compiler::pass::memory_scheduler::hint};

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

    compiler::pass::memory_scheduler::Config conf;
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

size_t
get_compute_code_distance(std::string_view regime)
{
    if (regime == "T")
        return 23;
    else if (regime == "G")
        return 17;
    else if (regime == "M")
        return 11;
    else
        std::cerr << "get_compute_code_distance: unknown regime \"" << regime << "\"" << _die{};
}

size_t
get_memory_code_distance(std::string_view regime)
{
    if (regime == "T")
        return 24;
    else if (regime == "G")
        return 18;
    else if (regime == "M")
        return 12;
    else
        std::cerr << "get_compute_code_distance: unknown regime \"" << regime << "\"" << _die{};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<sim::ProducerQuantum>
get_default_factory_specifications(std::string_view regime,
                                    int64_t /* compute_cycle_time_ns -- unused: BulkProducer runs every level
                                                on a single clock, so ProducerQuantum carries cycle *counts*,
                                                not a per-level ns clock */,
                                    int64_t ll_buffer_capacity)
{
    if (regime == "G")
        return { sim::t_cc_cultivation(5) };

    sim::ProducerQuantum l1 = sim::t_cc_cultivation(3);              // d = 3 color code cultivation
    sim::ProducerQuantum l2 = sim::t_15to1_distillation(25, 11, 11, l1.infidelity);  // 15:1, (dx,dz,dm) = (25,11,11)
    l2.buffer_size = ll_buffer_capacity;

    return {l1, l2};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
